AliAnalysisTaskPHOSPi0EtaToGammaGamma* AddTaskPHOSPi0EtaToGammaGamma_pp_5TeV(
    const char* name     = "Pi0EtaToGammaGamma",
    UInt_t trigger = AliVEvent::kINT7,
    const TString CollisionSystem = "pp",
    const Bool_t isMC = kFALSE,
    const TString triggerinput = "",//L1H,L1M,L1L,L0
    const Float_t CenMin = 0.,
    const Float_t CenMax = 90.,
    const Int_t NMixed   = 10,
    const Bool_t FlowTask = kFALSE,
    const Int_t harmonics = -1,
    const Bool_t useCoreE = kFALSE,
    const Bool_t useCoreDisp = kFALSE,
    const Double_t NsigmaCPV  = 2.5,
    const Double_t NsigmaDisp = 2.5,
    const Bool_t usePHOSTender = kTRUE,
    const Bool_t TOFcorrection = kTRUE,
    const Bool_t NonLinStudy = kFALSE,
    const Double_t bs = 25.,//bunch space in ns.
    const Double_t distBC = 0,//minimum distance to bad channel.
    const Bool_t isJJMC = kFALSE,
    const TString MCtype = "MBMC"
    )
{
  //Add a task AliAnalysisTaskPHOSPi0EtaToGammaGamma to the analysis train
  //Author: Daiki Sekihata
  /* $Id$ */

  AliAnalysisManager *mgr = AliAnalysisManager::GetAnalysisManager();
  if (!mgr) {
    ::Error("AddTaskPHOSPi0EtaToGammaGamma", "No analysis manager to connect to");
    return NULL;
  }
  
  if (!mgr->GetInputEventHandler()) {
    ::Error("AddTaskPHOSPi0EtaToGammaGamma", "This task requires an input event handler");
    return NULL;
  }
  const Bool_t Trgcorrection = kTRUE;

	TString TriggerName="";
	if     (trigger == (UInt_t)AliVEvent::kAny)  TriggerName = "kAny";
	else if(trigger == (UInt_t)AliVEvent::kINT7) TriggerName = "kINT7";
	else if(trigger == (UInt_t)AliVEvent::kPHI7) TriggerName = "kPHI7";

  if(trigger == (UInt_t)AliVEvent::kPHI7){
    if(triggerinput.Contains("L1") || triggerinput.Contains("L0")){
      TriggerName = TriggerName + "_" + triggerinput;
    }
    else{
      ::Error("AddTaskPHOSPi0EtaToGammaGamma", "PHOS trigger analysis requires at least trigger input (L0 or L1[H,M,L]).");
      return NULL;
    }
  }

  Int_t systemID = -1;
  if(CollisionSystem=="pp")                                 systemID = 0;
  else if(CollisionSystem=="PbPb")                          systemID = 1;
  else if(CollisionSystem=="pPb" || CollisionSystem=="Pbp") systemID = 2;

  TString PIDname="";
  if(NsigmaCPV > 0) PIDname += Form("_CPV%d",(Int_t)(NsigmaCPV*10));
  if(NsigmaDisp > 0){
    if(useCoreDisp) PIDname += Form("_CoreDisp%d",(Int_t)(NsigmaDisp*10));
    else            PIDname += Form("_FullDisp%d",(Int_t)(NsigmaDisp*10));
  }
  if(useCoreE) PIDname += "_CoreE";
  else         PIDname += "_FullE";

  TString taskname = "";
  if(FlowTask){
     if(harmonics > 0) taskname = Form("%s_%s_%s_Cen%d_%d%s_Harmonics%d_BS%dns_DBC%dcell",name,CollisionSystem.Data(),TriggerName.Data(),(Int_t)CenMin,(Int_t)CenMax,PIDname.Data(),harmonics,(Int_t)bs,(Int_t)(distBC));
      else{
        ::Error("AddTaskPHOSPi0EtaToGammaGamma", "Qn flow vector correction is ON, but you do not set harmonics.");
        return NULL;
      }
  }
  else taskname = Form("%s_%s_%s_Cen%d_%d%s_BS%dns_DBC%dcell",name,CollisionSystem.Data(),TriggerName.Data(),(Int_t)CenMin,(Int_t)CenMax,PIDname.Data(),(Int_t)bs,(Int_t)(distBC));

  AliAnalysisTaskPHOSPi0EtaToGammaGamma* task = new AliAnalysisTaskPHOSPi0EtaToGammaGamma(taskname);

  if(trigger == (UInt_t)AliVEvent::kPHI7) task->SetPHOSTriggerAnalysis(triggerinput,isMC);
  if(kMC && trigger == (UInt_t)AliVEvent::kPHI7) trigger = AliVEvent::kINT7;//change trigger selection in MC when you do PHOS trigger analysis.

  task->SelectCollisionCandidates(trigger);

  task->SetCollisionSystem(systemID);//colliions system : pp=0, PbPb=1, pPb (Pbp)=2;
  task->SetJetJetMC(isJJMC);
  task->SetMCType(MCtype);
  task->SetNonLinearityStudy(NonLinStudy);
 
  task->SetTenderFlag(usePHOSTender);
  task->SetMCFlag(isMC);
  task->SetCoreEnergyFlag(useCoreE);

  task->SetEventCuts(isMC);
  task->SetClusterCuts(useCoreDisp,NsigmaCPV,NsigmaDisp,distBC);

  task->SetCentralityMin(CenMin);
  task->SetCentralityMax(CenMax);
  task->SetDepthNMixed(NMixed);
  task->SetQnVectorTask(FlowTask);
  task->SetHarmonics(harmonics);

  //centrality setting
  task->SetCentralityEstimator("HybridTrack");

  //setting esd track selection for hybrid track
  gROOT->LoadMacro("$ALICE_PHYSICS/PWGJE/macros/CreateTrackCutsPWGJE.C");
  AliESDtrackCuts *cutsG = CreateTrackCutsPWGJE(10001008);//for good global tracks
  task->SetESDtrackCutsForGlobal(cutsG);
  AliESDtrackCuts *cutsGC = CreateTrackCutsPWGJE(10011008);//for good global-constrained tracks
  task->SetESDtrackCutsForGlobalConstrained(cutsGC);

  //bunch space for TOF cut
  task->SetBunchSpace(bs);//in unit of ns.
  if(!isMC && TOFcorrection){
    TF1 *f1tof = new TF1("f1TOFCutEfficiency","[0] * (2/(1+exp(-[1]*(x-[2]))) - 1) - ( 0 + [3]/(exp( -(x-[4]) / [5] ) + 1)  )",0,100);
    f1tof->SetNpx(1000);
    f1tof->SetParameters(0.996,2.33,4.15e-3,0.477,7.57,0.736);
    task->SetTOFCutEfficiencyFunction(f1tof);
    //printf("TOF cut efficiency as a function of E is %s\n",f1tof->GetTitle());
  }
  if(!isMC && Trgcorrection){
    TF1 *f1trg = new TF1("f1TriggerEfficiency","[0]/(TMath::Exp(-(x-[1])/[2]) + 1)",0,100);
    f1trg->SetNpx(1000);
    f1trg->SetParameters(0.999,2.49,0.29);
    task->SetTriggerEfficiency(f1trg);
    //printf("TOF cut efficiency as a function of E is %s\n",f1tof->GetTitle());
  }

  if(isMC){
    //for pi0 pT weighting
    const Int_t Ncen_Pi0 = 2;
    const Double_t centrality_Pi0[Ncen_Pi0] = {0,9999};
    TArrayD *centarray_Pi0 = new TArrayD(Ncen_Pi0,centrality_Pi0);

    TObjArray *farray_Pi0 = new TObjArray(Ncen_Pi0-1);
    TF1 *f1weightPi0[Ncen_Pi0-1];

    if(MCtype.Contains("P8") || MCtype.Contains("Pythia8")){
      printf("Pythia8 is selected.\n");
      const Double_t p0[Ncen_Pi0-1] = {-0.58};
      const Double_t p1[Ncen_Pi0-1] = { 0.76};
      const Double_t p2[Ncen_Pi0-1] = { 1.11};

      for(Int_t icen=0;icen<Ncen_Pi0-1;icen++){
        f1weightPi0[icen] = new TF1(Form("f1weightPi0_%d",icen),"[2]*(1.+[0]/(1. + TMath::Power(x/[1],2)))",0,100);//this is iterative procedure.
        f1weightPi0[icen]->SetParameters(p0[icen],p1[icen],p2[icen]);
        farray_Pi0->Add(f1weightPi0[icen]);
      }
      task->SetAdditionalPi0PtWeightFunction(centarray_Pi0,farray_Pi0);

      //for K/pi ratio
      const Int_t Ncen_K0S = 2;
      const Double_t centrality_K0S[Ncen_K0S] = {0,9999};
      TArrayD *centarray_K0S = new TArrayD(Ncen_K0S,centrality_K0S);

      TObjArray *farray_K0S = new TObjArray(Ncen_K0S-1);
      TF1 *f1weightK0S[Ncen_K0S-1];
      const Double_t p0[Ncen_K0S-1] = { 1.37};
      const Double_t p1[Ncen_K0S-1] = { 4.98};
      const Double_t p2[Ncen_K0S-1] = {0.156};
      const Double_t p3[Ncen_K0S-1] = { 2.79};
      const Double_t p4[Ncen_K0S-1] = {0.239};

      for(Int_t icen=0;icen<Ncen_K0S-1;icen++){
        f1weightK0S[icen] = new TF1(Form("f1weightK0S_%d",icen),"[0] * (2/(1+exp(-[1]*x)) - 1) - ( 0 + [2]/(exp( -(x-[3]) / [4] ) + 1) )",0,100);
        f1weightK0S[icen]->SetParameters(p0[icen],p1[icen],p2[icen],p3[icen],p4[icen]);
        farray_K0S->Add(f1weightK0S[icen]);
      }

      task->SetAdditionalK0SPtWeightFunction(centarray_K0S,farray_K0S);

    }
    else if(MCtype.Contains("P6") || MCtype.Contains("Pythia6")){
      printf("Pythia6 is selected.\n");
      const Double_t p0[Ncen_Pi0-1] = {0.22};
      const Double_t p1[Ncen_Pi0-1] = { 2.2};
      const Double_t p2[Ncen_Pi0-1] = { 1.2};
      const Double_t p3[Ncen_Pi0-1] = { 0.8};

      for(Int_t icen=0;icen<Ncen_Pi0-1;icen++){
        f1weightPi0[icen] = new TF1(Form("f1weightPi0_%d",icen),"[0]*exp(-pow((x-[1]),2)/[2])+[3]",0,100);
        f1weightPi0[icen]->SetParameters(p0[icen],p1[icen],p2[icen],p3[icen]);
        farray_Pi0->Add(f1weightPi0[icen]);
      }
      task->SetAdditionalPi0PtWeightFunction(centarray_Pi0,farray_Pi0);

      //for K/pi ratio
      const Int_t Ncen_K0S = 2;
      const Double_t centrality_K0S[Ncen_K0S] = {0,9999};
      TArrayD *centarray_K0S = new TArrayD(Ncen_K0S,centrality_K0S);

      TObjArray *farray_K0S = new TObjArray(Ncen_K0S-1);
      TF1 *f1weightK0S[Ncen_K0S-1];
      const Double_t p0[Ncen_K0S-1] = { 1.44};
      const Double_t p1[Ncen_K0S-1] = { 5.76};

      for(Int_t icen=0;icen<Ncen_K0S-1;icen++){
        f1weightK0S[icen] = new TF1(Form("f1weightK0S_%d",icen),"[0] * (2/(1+exp(-[1]*x)) - 1)",0,100);
        f1weightK0S[icen]->SetParameters(p0[icen],p1[icen]);
        farray_K0S->Add(f1weightK0S[icen]);
      }

      task->SetAdditionalK0SPtWeightFunction(centarray_K0S,farray_K0S);
    }

  }

  mgr->AddTask(task);
  mgr->ConnectInput(task, 0, mgr->GetCommonInputContainer() );
 
  TString outputFile = AliAnalysisManager::GetCommonFileName();
  TString prefix = Form("hist_%s",taskname.Data());

  AliAnalysisDataContainer *coutput1 = mgr->CreateContainer(Form("%s",prefix.Data()), THashList::Class(), AliAnalysisManager::kOutputContainer, Form("%s:%s",outputFile.Data(),"PWGGA_PHOSTasks_PHOSRun2"));
  mgr->ConnectOutput(task, 1, coutput1);

  return task;
}

