#!/bin/bash
#
#  - script to sync a group of files on alien with a local cache
#    downloads only new and updated files
#  - by default it mirrors the directory structure in a specified local location
#    (the local chache location and paths can be manipulated.)
#  - needs a configured config file (by default alienSync.config)
#    and a working alien environment (token and at least $ALIEN_DIR or $ALIEN_ROOT set)
#
#  origin: Mikolaj Krzewicki, mikolaj.krzewicki@cern.ch
#
if [ ${BASH_VERSINFO} -lt 4 ]; then
  echo "bash version >= 4 needed, you have ${BASH_VERSION}, exiting..."
  exit 1
fi

main()
{
  if [[ $# -lt 1 ]]; then
    echo "Usage:  ${0##*/} configFile=/path/to/config"
    echo "expert: ${0##*/} alienFindCommand=\"alien_find /some/path/ file\" [opt=value]"
    echo "        ${0##*/} alienFindCommand=\"alien_find /some/path/ file\" localPathPrefix=\${PWD}"
    echo
    echo "by default files are downloaded to current dir, or \${alienSync_localPathPrefix}, if set."
    echo "At least specify alienFindCommand, either on command line or in the configFile."
    echo "the logs go by default to localPathPrefix/alienSyncLogs"
    return
  fi
  
  #be nice and allow group members access as well (002 will create dirs with 775 and files with 664)
  umask 0002

  # try to load the config file
  #[[ ! -f $1 ]] && echo "config file $1 not found, exiting..." | tee -a $logFile && exit 1
  if ! parseConfig "$@"; then return 1; fi

  [[ -z ${alienFindCommand} ]] && echo "alienFindCommand not defined!" && return 1

  #if not set, use the default group
  [[ -z ${alienSyncFilesGroupOwnership} ]] && alienSyncFilesGroupOwnership=$(id -gn)

  # do some accounting
  [[ ! -d $logOutputPath ]] && echo "logOutputPath not available, creating..." && mkdir -p $logOutputPath && chgrp ${alienSyncFilesGroupOwnership} ${logOutputPath}
  [[ ! -d $logOutputPath ]] && echo "could not create log dir, exiting..." && exit 1
  dateString=$(date +%Y-%m-%d-%H-%M)
  logFile=$logOutputPath/alienSync-$dateString.log
  echo "$0 $@"|tee -a $logFile
  echo ""|tee -a $logFile
  echo log: $logFile
  
  #lock
  lockFile=$logOutputPath/runningNow.lock
  [[ -f $lockFile && ${allowConcurrent} -ne 1 ]] && echo "locked. Another process running? ($lockFile)" | tee -a $logFile && exit 1
  touch $lockFile
  [[ ! -f $lockFile ]] && echo "unable to create lock. exiting..." | tee -a $logFile && exit 1

  #redirect all output to a log
  if [[ $allOutputToLog -eq 1 ]]; then
    exec 6>&1
    exec 1>$logFile 2>&1
  fi
  
  newFilesList=$logOutputPath/"newFiles.list"
  rm -f $newFilesList
  touch $newFilesList
  redoneFilesList=$logOutputPath/"redoneFiles.list"
  rm -f $redoneFilesList
  touch $redoneFilesList
  updatedFilesList="${logOutputPath}/updatedFiles.list"
  failedDownloadList="${logOutputPath}/failedDownload.list"
  touch ${failedDownloadList}


  # check the config
  [[ -z $alienFindCommand ]] && echo "alienFindCommand not defined, exiting..." && exitScript 1
  [[ -z ${localPathPrefix} ]] && echo "localPathPrefix not defined, exiting..." && exitScript 1
  [[ -z $logOutputPath ]] && echo "logOutputPath not defined, exiting..." && exitScript 1
  [[ -z $secondsToSuicide ]] && echo "setting default secondsToSuicide of 10 hrs..." && secondsToSuicide=$(( 10*3600 ))

  # init alien 
  [[ -z $ALIEN_ROOT && -n $ALIEN_DIR ]] && ALIEN_ROOT=$ALIEN_DIR
  #if ! haveAlienToken; then
  #  $ALIEN_ROOT/api/bin/alien-token-destroy
    $ALIEN_ROOT/api/bin/alien-token-init $alienUserName
  #fi
  #if ! haveAlienToken; then
  #  if [[ $allOutputToLog -eq 1 ]]; then
  #    exec 1>&6 6>&-
  #  fi
  #  echo "problems getting token! exiting..." | tee -a $logFile
  #  exitScript 1
  #fi
  #ls -ltr /tmp/gclient_env_$UID
  #cat /tmp/gclient_env_$UID
  source /tmp/gclient_env_$UID

  #set a default timeout for grid access
  [[ -z $copyTimeout ]] && copyTimeout=600
  export GCLIENT_COMMAND_MAXWAIT=$copyTimeout

  localAlienDatabase=$logOutputPath/localAlienDatabase.list
  localFileList=$logOutputPath/localFile.list
  
  alienFileListCurrent=$logOutputPath/alienFileDatabase.list
  [[ ! -f $localFileList ]] && touch $localFileList
  candidateLocalFileDatabase=$logOutputPath/candidateLocalFileDatabase.list

  #here we produce the current alien file list
  if [[ -n ${useExistingAlienFileDatabase} && -f ${localAlienDatabase} ]]; then
    #we use the old one
    echo "using ${localAlienDatabase} instead of full alien search"
    echo cp -f ${localAlienDatabase} ${alienFileListCurrent}
    cp -f ${localAlienDatabase} ${alienFileListCurrent}
  else
    #we make a new one
    echo "eval $alienFindCommand > $alienFileListCurrent"
    eval "$alienFindCommand" > $alienFileListCurrent
  fi

  echo "number of files in the collection: $(wc -l $alienFileListCurrent)"
  #create a list of candidate destination locations
  #this is in case there are more files on alien trying to get to the same local destination
  #in which case we take the one with the youngest ctime (later in code)
  if [[ -n ${destinationModifyCommand} ]]; then
    echo eval "cat $alienFileListCurrent | ${destinationModifyCommand} | sed \"s,^,${localPathPrefix},\"  > ${candidateLocalFileDatabase}"
    eval "cat $alienFileListCurrent | ${destinationModifyCommand} | sed \"s,^,${localPathPrefix},\"  > ${candidateLocalFileDatabase}"
  fi

  #logic is: if file list is missing we force the md5 recalculation
  [[ ! -f $localAlienDatabase ]] && forceLocalMD5recalculation=1 && echo "forcing local MD5 sum recalculation" && cp -f $alienFileListCurrent $localAlienDatabase

  #since we grep through the files frequently, copy some stuff to tmpfs for fast access
  tmp=$(mktemp -d 2>/dev/null)
  if [[ -d $tmp ]]; then
    cp $localAlienDatabase $tmp
    cp $localFileList $tmp
    cp $alienFileListCurrent $tmp
    [[ -f ${candidateLocalFileDatabase} ]] && cp ${candidateLocalFileDatabase} ${tmp}
  else
    tmp=$logOutputPath
  fi

  echo "starting downloading:"
  lineNumber=0
  alienFileCounter=0
  localFileCounter=0
  downloadedFileCounter=0
  while read -r alienFile md5alien timestamp size
  do
    ((lineNumber++))
    
    #sometimes the md5 turns out empty and is then stored as a "." to avoid problems parsing
    [[ "$md5alien" =~ "." ]] && md5alien=""
    
    [[ -n $timeStampInLog ]] && date
    [[ $SECONDS -ge $secondsToSuicide ]] && echo "$SECONDS seconds passed, exiting by suicide..." && break
    [[ "$alienFile" != "/"*"/"?* ]] && echo "WARNING: read line not path-like: $alienFile" && continue
    ((alienFileCounter++))
    destination=${localPathPrefix}/${alienFile}
    destination=${destination//\/\///} #remove double slashes
    [[ -n ${destinationModifyCommand} ]] && destination=$( eval "echo ${destination} | ${destinationModifyCommand}" )
    destinationdir=${destination%/*}
    [[ -n $softLinkName ]] && softlinktodestination=${destinationdir}/${softLinkName}
    tmpdestination="${destination}.aliensyncTMP"
    
    #if we allow concurrent running (DANGEROUS) check if somebody is already trying to process this file
    if [[ -f ${tmpdestination} && ${allowConcurrent} -eq 1 ]]; then 
      echo "$tmpdestination exists - concurrent donwload? skipping..."
      continue
    fi

    if [[ -n ${destinationModifyCommand} ]]; then
      #find the candidate in the database, in case there are more files trying to go to the same
      #place due to $destinationModifyCommand which alters the final path, find the one
      #with the largest ctime (3rd field in the database list) and check if that is the current one
      #if not - skip
      #echo grep -n ${destination} $candidateLocalFileDatabase | sed "s/:/ /"  | sort -rk4
      #grep -n ${destination} $candidateLocalFileDatabase| sed "s/:/ /"  | sort -rk4
      #this guy contains: index of the original entry, local file name, md5, ctime
      candidateDBrecord=($(grep -n ${destination} $tmp/${candidateLocalFileDatabase##*/}| sed "s/:/ /"  | sort -rk4|head -n1 ))
      originalEntryIndex=${candidateDBrecord[0]}
      [[ $lineNumber -ne $originalEntryIndex ]] && continue
    fi
    
    redownloading=""
    if [[ -f ${destination} ]]; then
      #soft link the downloaded file (maybe to provide a consistent link to the latest version)
      if [[ -n $softlinktodestination ]]; then
        echo ln -sf ${destination} ${softlinktodestination}
        ln -sf ${destination} ${softlinktodestination}
      fi
      ((localFileCounter++))
      
      localDBrecord=($(grep $alienFile $tmp/${localAlienDatabase##*/}))
      md5local=${localDBrecord[1]}

      #sometimes the md5 turns out empty and is then stored as a "." to avoid problems parsing
      [[ "$md5local" =~ "." ]] && md5local=""

      if [[ $forceLocalMD5recalculation -eq 1 || -z $md5local ]]; then
        md5recalculated=$(checkMD5sum ${destination})
        [[ "$md5local" != "$md5recalculated" ]] && echo "WARNING: local copy change ${destination}"
        md5local=${md5recalculated}
      fi
      if [[ "$md5local" == "$md5alien" && -n $md5alien ]]; then
        echo "OK ${destination} $md5alien"
        if ! grep -q ${destination} $tmp/${localFileList##*/}; then
          echo ${destination} >> $localFileList
        fi
        continue
      fi
      if [[ -z $md5alien ]]; then
        if ! grep -q ${destination} $tmp/${localFileList##*/}; then
          echo ${destination} >> $localFileList
        fi
        echo "WARNING: missing alien md5, leaving the local file as it is"
        continue
      fi
      echo "WARNING: md5 mismatch ${destination}"
      echo "  $md5local $md5alien"
      redownloading=1
    fi
    
    [[ -f $tmpdestination ]] && echo "WARNING: stale $tmpdestination, removing" && rm $tmpdestination
    
    mkdir -p ${destinationdir} && chgrp ${alienSyncFilesGroupOwnership} ${destinationdir}
    [[ ! -d $destinationdir ]] && echo cannot access $destinationdir && continue

    #check token
    #if ! haveAlienToken; then
    #  $ALIEN_ROOT/api/bin/alien-token-init $alienUserName
    #  #source /tmp/gclient_env_$UID
    #fi
    
    export copyMethod
    export copyScript
    export copyTimeout
    export copyTimeoutHard
    echo copyFromAlien "$alienFile" "$tmpdestination"
    [[ $pretend -eq 1 ]] && continue
    copyFromAlien $alienFile $tmpdestination
    chgrp ${alienSyncFilesGroupOwnership} $tmpdestination

    # if we didn't download remove the destination in case we tried to redownload 
    # a corrupted file
    [[ ! -f $tmpdestination ]] && echo "file not downloaded" && rm -f ${destination} && continue

    downloadOK=0
    #verify the downloaded md5 if available, validate otherwise...
    if [[ -n $md5alien ]]; then
      md5recalculated=$(checkMD5sum ${tmpdestination})
      if [[ ${md5alien} == ${md5recalculated} ]]; then
        echo "OK md5 after download"
        downloadOK=1
      else
        echo "failed verifying md5 $md5alien of $tmpdestination"
      fi
    else
      downloadOK=1
    fi

    #handle zip files - check the checksums
    if [[ $alienFile =~ '.zip' && $downloadOK -eq 1 ]]; then
      echo "checking integrity of zip archive $tmpdestination"
      if unzip -t $tmpdestination; then
        downloadOK=1
      else
        downloadOK=0
      fi
    fi

    if [[ $downloadOK -eq 1 ]]; then
      echo mv $tmpdestination ${destination}
      mv $tmpdestination ${destination}
      chgrp ${alienSyncFilesGroupOwnership} ${destination}
      ((downloadedFileCounter++))
      if [[ -n $softlinktodestination ]]; then
        echo ln -s ${destination} $softlinktodestination
        ln -s ${destination} $softlinktodestination
      fi
      [[ -z $redownloading ]] && echo ${destination} >> $newFilesList
      [[ -n $redownloading ]] && echo ${destination} >> $redoneFilesList
      if ! grep -q ${destination} $tmp/${localFileList##*/}; then
        echo ${destination} >> $localFileList
      fi
      [[ -n ${postCommand} ]] && ( cd ${destinationdir}; eval "${postCommand}" )
      if grep -q ${alienFile} ${failedDownloadList}; then
        echo "removing ${alienFile} from ${failedDownloadList}"
        grep -v ${alienFile} ${failedDownloadList} >tmpUpdatedFailed
        mv tmpUpdatedFailed ${failedDownloadList}
      fi
    else
      echo "download not validated, NOT moving to ${destination}..."
      echo "removing $tmpdestination"
      rm -f $tmpdestination
      echo ${alienFile} >> ${failedDownloadList}
      continue
    fi

    [[ -f $tmpdestination ]] && echo "WARNING: tmpdestination should not still be here! removing..." && rm -r ${tmpdestination}

    if [[ $unzipFiles -eq 1 ]]; then
      echo unzip -o ${destination} -d ${destinationdir}
      unzip -o ${destination} -d ${destinationdir}
    fi

    echo
  done < ${alienFileListCurrent}

  [[ $alienFileCounter -gt 0 ]] && mv -f $alienFileListCurrent $localAlienDatabase

  echo "${0##*/} DONE"
 
  if [[ $allOutputToLog -eq 1 ]]; then
    exec 1>&6 6>&-
  fi
 
  cat ${newFilesList} ${redoneFilesList} > ${updatedFilesList}
  
  echo alienFindCommand:
  echo "  $alienFindCommand"
  echo
  echo "files on alien: $alienFileCounter"
  echo "local files before: $localFileCounter"
  echo "files downloaded: $downloadedFileCounter"
  echo
  echo "new files:"
  echo
  cat $newFilesList
  echo
  echo "redone files:"
  echo
  cat $redoneFilesList
  echo
  echo
  
  #output the list of failed files to stdout, so the cronjob can mail it
  echo '###############################'
  echo "failed to download from alien:"
  echo
  local tmpfailed=$(mktemp)
  [[ "$(cat ${failedDownloadList} | wc -l)" -gt 0 ]] && sort ${failedDownloadList} | uniq -c | awk 'BEGIN{print "#tries\t file" }{print $1 "\t " $2}' | tee ${tmpfailed}
  
  [[ -n ${MAILTO} ]] && echo $logFile | mail -s "alienSync ${alienFindCommand} done" ${MAILTO}

  echo
  echo
  echo '###############################'
  echo "eval ${executeEnd}"
  eval "${executeEnd}"

  exitScript 0
}

exitScript()
{
  echo
  echo removing $lockFile
  rm -f $lockFile
  echo removing $tmp
  rm -rf $tmp
  exit $1
}

alien_find()
{
  # like a regular alien_find command
  # output is a list with md5 sums and ctimes
  executable="$ALIEN_ROOT/api/bin/gbbox find"
  [[ ! -x ${executable% *} ]] && echo "### error, no $executable..." && return 1
  [[ -z $logOutputPath ]] && logOutputPath="./"

  maxCollectionLength=10000

  export GCLIENT_COMMAND_MAXWAIT=600
  export GCLIENT_COMMAND_RETRY=20
  export GCLIENT_SERVER_RESELECT=4
  export GCLIENT_SERVER_RECONNECT=2
  export GCLIENT_RETRY_DAMPING=1.2
  export GCLIENT_RETRY_SLEEPTIME=2

  iterationNumber=0
  numberOfFiles=$maxCollectionLength
  rm -f $logOutputPath/alien_find.err
  while [[ $numberOfFiles -ge $maxCollectionLength && $iterationNumber -lt 100 ]]; do
    numberOfFiles=0
    offset=$((maxCollectionLength*iterationNumber-1)); 
    [[ $offset -lt 0 ]] && offset=0; 
    $executable -x coll -l ${maxCollectionLength} -o ${offset} "$@" 2>>$logOutputPath/alien_find.err \
    | while read -a fields;
    do
      nfields=${#fields[*]}
      turl=""
      md5=""
      ctime=""
      size=""
      for ((x=1;x<=${nfields};x++)); do
        field=${fields[${x}]}
        if [[ "${field}" == "md5="* ]]; then
          eval ${field}
        fi
        if [[ "${field}" == "turl="* ]]; then
          eval ${field}
        fi
        if [[ "${field}" == "ctime="* ]]; then
          eval ${field}" "${fields[((x+1))]}
        fi
        if [[ "${field}" == "size="* ]]; then
          eval ${field}" "${fields[((x+1))]}
        fi
      done
      ctime=$( date -d "${ctime}" +%s 2>/dev/null)
      [[ -z $md5 ]] && md5="."
      [[ -n "$turl" ]] && echo "${turl//"alien://"/} ${md5} ${ctime} ${size}" && ((numberOfFiles++))
    done
    ((iterationNumber++))
  done
  return 0
}

alien_find_split()
{
  #split the search in sub searches in the subdirectories of the base path
  basePath=${1}
  searchTerm=${2}
  subPathSelection=${3}
  [[ -z ${subPathSelection} ]] && subPathSelection=".*"
  gbbox ls ${basePath} 2>/dev/null | \
  while read subPath; do
    [[ ! ${subPath} =~ ${subPathSelection} ]] && continue
    alien_find ${basePath}/${subPath} ${searchTerm}
  done 
}

listCollectionContents()
{
  #find the xml collections and print the list of filenames and hashes
  while read -a fields; do
    nfields=${#fields[*]}
    turl=""
    md5=""
    ctime=""
    for ((x=1;x<=${nfields};x++)); do
      field=${fields[${x}]}
      if [[ "${field}" == "md5="* ]]; then
        eval ${field}
      fi
      if [[ "${field}" == "turl="* ]]; then
        eval ${field}
      fi
      if [[ "${field}" == "ctime="* ]]; then
        eval "${field} ${fields[((x+1))]}"
      fi
    done
    ctime=$( date -d "${ctime}" +%s 2>/dev/null)
    [[ -n "$turl" ]] && echo "${turl//"alien://"/} ${md5} ${ctime}"
  done < <(catCollections $1 $2 2>/dev/null)
}

catCollections()
{
  #print the contents of collection(s)
  if [[ $# -eq 2 ]]; then
    while read collection; do
      [[ $collection != "/"*"/"?* ]] && continue
      gbbox cat $collection
    done < <(alien_find $1 $2)
  elif [[ $# -eq 1 ]]; then
    gbbox cat $1
  fi
}

haveAlienToken()
{
  #only get a new token if the old one expires soon
  maxExpireTime=$1
  [[ -z $maxExpireTime ]] && maxExpireTime=4000
  [[ -z $ALIEN_ROOT ]] && echo "no ALIEN_ROOT!" && return 1
  now=$(date "+%s")
  tokenExpirationTime=$($ALIEN_ROOT/api/bin/alien-token-info|grep Expires)
  tokenExpirationTime=$(date -d "${tokenExpirationTime#*:}" "+%s")
  secondsToExpire=$(( tokenExpirationTime-now ))
  if [[ $secondsToExpire -lt $maxExpireTime ]]; then
    return 1
  else
    echo "token valid for another $secondsToExpire seconds"
    return 0
  fi
}

copyFromAlien()
{
  #copy the file $1 to $2 using a specified method
  #uses the "timeout" command to make sure the 
  #download processes will not hang forever.
  #
  [[ -z $copyTimeout ]] && copyTimeout=600
  [[ -z $copyTimeoutHard ]] && copyTimeoutHard=1200
  src=${1//"alien://"/}
  src="alien://${src}"
  dst=$2
  if [[ "$copyMethod" == "tfilecp" ]]; then
    if which timeout &>/dev/null; then
      echo timeout $copyTimeout root -b -q "$copyScript(\"$src\",\"$dst\")"
      timeout $copyTimeout root -b -q "$copyScript(\"$src\",\"$dst\")"
    else
      echo root -b -q "$copyScript(\"$src\",\"$dst\")"
      root -b -q "$copyScript(\"$src\",\"$dst\")"
    fi
  else
    if which timeout &>/dev/null; then
      echo timeout $copyTimeout $ALIEN_ROOT/api/bin/alien_cp $src $dst
      timeout $copyTimeout $ALIEN_ROOT/api/bin/alien_cp $src $dst
    else
      echo $ALIEN_ROOT/api/bin/alien_cp $src $dst
      $ALIEN_ROOT/api/bin/alien_cp $src $dst
    fi
  fi
}

parseConfig()
{
  #config file
  configFile=""
  alienFindCommand=""
  secondsToSuicide=$(( 10*3600 ))
  localPathPrefix="${PWD}"
  #define alienSync_localPathPrefix in your env to have a default central location
  [[ -n ${alienSync_localPathPrefix} ]] && localPathPrefix=${alienSync_localPathPrefix}
  unzipFiles=0
  allOutputToLog=0

  args=("$@")

  #first, check if the config file is configured
  #is yes - source it so that other options can override it
  #if any
  for opt in "${args[@]}"; do
    if [[ ${opt} =~ configFile=.* ]]; then
      eval "${opt}"
      [[ ! -f ${configFile} ]] && echo "configFile ${configFile} not found, exiting..." && return 1
      echo "using config file: ${configFile}"
      source "${configFile}"
      break
    fi
  done

  #then, parse the options as they override the options from file
  for opt in "${args[@]}"; do
    if [[ ! "${opt}" =~ .*=.* ]]; then
      echo "badly formatted option ${var}, should be: option=value, stopping..."
      return 1
    fi
    local var="${opt%%=*}"
    local value="${opt#*=}"
    echo "${var} = ${value}"
    export ${var}="${value}"
  done

  #things that by default depend on other variables should be set here, after the dependencies
  [[ -z ${logOutputPath} ]] && logOutputPath="${localPathPrefix}/alienSyncLogs"
  return 0
}

checkMD5sum()
{
  local file="${1}"
  local md5=""
  [[ ! -f ${file} ]] && return 1
  if which md5sum &>/dev/null; then
    local tmp=($(md5sum ${file}))
    md5=${tmp[0]}
  elif which md5 &>/dev/null; then
    local tmp=($(md5 ${file}))
    md5=${tmp[3]}
  fi
  echo ${md5}
}

main "$@"
