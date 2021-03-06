#ifndef __CINT__
# include <TFile.h>
# include <THStack.h>
# include <TH1.h>
# include <TError.h>
# include <TMath.h>
# include <TClass.h>
# include <TCanvas.h>
# include <TSystem.h>
#else
class TFile;
class THStack;
class TH1;
class TCanvas;
#endif

const Bool_t kCompareVarLoaded = true;
Int_t cW = 1200;
Int_t cH =  800;
const char* obs = "\\mathrm{d}N_{\\mathrm{ch}}/\\mathrm{d}\\eta";

//____________________________________________________________________
/** 
 * Print a histogram 
 * 
 * @param h    Histgram to print 
 * @param prec Precision to use 
 */
void PrintH(TH1* h, Int_t prec=2)
{
  Printf("Content of %s - %s", h->GetName(), h->GetTitle());
  for (Int_t i = 1; i <= h->GetNbinsX(); i++) {
    if (h->GetBinContent(i) <= 1e-6) continue;
    printf("%3d (%+6.3f): %.*f+/-%.*f\n", i,
	   h->GetXaxis()->GetBinCenter(i),
	   prec, h->GetBinContent(i),
	   prec, h->GetBinError(i));
  }
}

TObject* GetO(TDirectory* dir, const char* name="result", TClass* cls=0)
{
  if (!dir) {
    Warning("GetHS", "No directory");
    return 0;
  }
  TString par; par = gSystem->DirName(name);
  TString bse; bse = gSystem->BaseName(name);

  if (!par.EqualTo(".")) {
    TDirectory* save = dir;
    dir = save->GetDirectory(par);
    if (!dir) {
      Warning("", "Failed to get directory %s", par.Data());
      // save->ls();
      return 0;
    }
  }
  TObject* o = dir->Get(bse);
  if (!o) {
    Warning("GetHS", "%s not found in %s", name, dir->GetName());
    return 0;
  }
  if (!cls) return o;
  if (!o->IsA()->InheritsFrom(cls)) {
    Warning("GetHS", "%s is not a %s!", name, cls->GetName());
    return 0;
  }
  return o;
}

THStack* GetHS(TDirectory* dir, const char* name="result")
{
  return static_cast<THStack*>(GetO(dir,name,THStack::Class()));
}
TH1* GetH1(TDirectory* dir, const char* name="result")
{
  return static_cast<TH1*>(GetO(dir,name,TH1::Class()));
}

TList* GetHists(UShort_t    flags,
		const char* var,
		const char* stackName="result",
		const char* sub="")
{
  const char* filename = Form("results/combine_%s_0x%x.root", var, flags);
  TFile* file = TFile::Open(filename, "READ");
  if (!file) return 0;

  THStack* stack = GetHS(file, Form("%s%s",sub,stackName));
  if (!stack) return 0;
  
  return stack->GetHists();  
}

TH1* GetMid(UShort_t flags,
	    const char* var)
{
  const char* filename = Form("results/combine_%s_0x%x.root", var, flags);
  TFile* file = TFile::Open(filename, "READ");
  if (!file) return 0;

  return GetH1(file, "mid");
}
TH1* GetCent(UShort_t flags,
	    const char* var)
{
  const char* filename = Form("results/combine_%s_0x%x.root", var, flags);
  TFile* file = TFile::Open(filename, "READ");
  if (!file) return 0;

  return GetH1(file, "cent");
}

void AddLine(TH1* h)
{
  TLine* l = new TLine(h->GetXaxis()->GetXmin(), 1,
		       h->GetXaxis()->GetXmax(), 1);;
  l->SetLineStyle(7);
  l->SetLineWidth(2);
  h->GetListOfFunctions()->Add(l);
}
  
TH1* Compare(TH1* def, TH1* oth) 
{
  // Printf("dividing %s by %s", oth->GetName(), def->GetName());  
  TH1* r    = static_cast<TH1*>(oth->Clone());
  r->SetDirectory(0);
  r->Divide(def);
  for (Int_t i = 1; i <= def->GetNbinsX(); i++) {
    Double_t cd = def->GetBinContent(i);
    Double_t co = oth->GetBinContent(i);
    Double_t ed = def->GetBinError(i);
    Double_t eo = oth->GetBinError(i);
    if (cd < 1e-6 || co < 1e-6) {
      r->SetBinContent(i, 0);
      r->SetBinError  (i, 0);
    }

    Double_t ch = r->GetBinContent(i);
    r->SetBinError(i, TMath::Max(ed/cd*ch,eo/co*ch));
  }
  // Printf("Returning %p (%s)", r, r->GetName());
  return r;
}


void CompareVars(UShort_t    flags,
		 const char* var,
		 Bool_t      noTruth=true)
{
  TList* defs   = GetHists(flags, "none");
  TList* others = GetHists(flags, var);
  if (!defs || !others) return;

  TIter nextDef(defs);
  TIter nextOth(others);
  THStack* result = new THStack(Form("compare_%s_0x%x",
				     var, flags),
				Form("\\hbox{%s vs. default }%s\\hbox{ %s}",
				     var, obs, 
				     (flags & 0x3) == 0x3 ?
				     "combinatorics" :
				     "injection"));
  TH1* def = 0;
  TH1* oth = 0;
  while ((def = static_cast<TH1*>(nextDef())) &&
	 (oth = static_cast<TH1*>(nextOth()))) {
    if (noTruth && TString(def->GetName()).EqualTo("truth")) continue;
    def->SetName("default");
    oth->SetName(var);
    TH1* h = Compare(def,oth);
    h->SetYTitle(Form("%s / default", var));
    result->Add(h);
  }
  TCanvas* c = new TCanvas(result->GetName(),result->GetTitle(), cW, cH);
  c->SetTopMargin(0.01);
  c->SetRightMargin(0.01);
  c->SetTicks();
  result->Draw("nostack");
  result->GetHistogram()->SetMinimum(0.9);
  result->GetHistogram()->SetMaximum(1.2);
  result->GetHistogram()->GetYaxis()->SetTitleOffset(1.5);
  result->GetHistogram()->SetXTitle("\\eta");
  result->GetHistogram()->SetYTitle(Form("%s / default", var));
  result->SetMinimum(0.9);
  result->SetMaximum(1.1);
  AddLine(result->GetHistogram());
  c->Modified();
  c->Print(Form("plots/%s.png", result->GetName()));
}

TH1* GetPubl()
{
  Int_t    nbin   = 9;
  Double_t bins[] = { 0., 5., 10., 20., 30., 40., 50., 60., 70., 80.,  };
  Double_t vals[] = { 1948, 1590, 1180, 786, 512, 318, 183, 96.3, 44.9, };
  Double_t errs[] = {   38,   32,   31,  20,  15,  12,   8,  5.8,  3.4, };
  TH1* ret = new TH1D("published", "dNch/deta in |eta|<0.5",
		      nbin, bins);
  for (Int_t i = 0; i < nbin; i++) {
    ret->SetBinContent(i+1, vals[i]);
    ret->SetBinError  (i+1, errs[i]);
  }
  return ret;
}
void PrintAxis(TAxis* axis)
{
  printf("%3d bins: ");
  for (Int_t i = 1; i <= axis->GetNbins(); i++)
    printf("%6.3f-", axis->GetBinLowEdge(i));
  Printf("%6.3f", axis->GetBinUpEdge(i));
}
void CompareMids(UShort_t    flags,
		 const char* var)
{
  TH1* def   = GetPubl(); // GetMid(flags, "none");
  TH1* other = GetMid(flags, var);
  if (!def || !other) return;
  // PrintAxis(def->GetXaxis());
  // PrintAxis(other->GetXaxis());
  
  def->SetName("default");
  other->SetName(var);
  TH1* hs = static_cast<TH1*>(def->Clone("ratioSys"));
  TH1* ht = static_cast<TH1*>(def->Clone("ratioStat"));
  hs->SetTitle("Syst.uncer.");
  ht->SetTitle("Stat.error");
  hs->SetStats(0);
  ht->SetStats(0);
  ht->SetMarkerStyle(20);
  ht->SetMarkerSize(2);
  ht->SetMarkerColor(kBlack);
  ht->SetLineColor(kBlack);
  ht->SetLineWidth(2);
  hs->SetFillColor(kBlue-10);
  hs->SetFillStyle(1001);
  hs->SetYTitle(Form("%s / published", var));
  ht->SetYTitle(Form("%s / published", var));
  hs->Reset();
  ht->Reset();
  for (Int_t i = 1; i <= def->GetNbinsX(); i++) {
    Double_t oc = other->GetBinContent(i);
    Double_t oe = other->GetBinError  (i);
    Double_t dc = def  ->GetBinContent(i);
    Double_t de = def  ->GetBinError  (i);
    Double_t y  = oc / dc;
    Double_t st = oe/oc*y;
    Double_t sy = de/dc*y;
    hs->SetBinContent(i, y);
    ht->SetBinContent(i, y);
    hs->SetBinError  (i, sy);
    ht->SetBinError  (i, st);
  }
  THStack* stack = new THStack(Form("mid_%s_0x%x", var, flags), "");
  stack->Add(hs, "e2");
  stack->Add(ht);
  AddLine(ht);
  // PrintH(def);
  // PrintH(other);
  // PrintH(h);
  
  TCanvas* c = new TCanvas(ht->GetName(),hs->GetTitle(), cW, cH);
  c->SetTopMargin(0.01);
  c->SetRightMargin(0.01);
  c->SetTicks();
  stack->SetMinimum(0.9);
  stack->SetMaximum(1.1);
  stack->Draw("nostack");
  TH1* h = stack->GetHistogram();
  // h->SetMinimum(0.9);
  // h->SetMaximum(1.1);
  h->GetYaxis()->SetTitleOffset(1.5);
  h->SetXTitle("Centrality [%]");
  h->SetYTitle(Form("%s / published", var));

  TLegend* l = c->BuildLegend(c->GetLeftMargin(),
			      c->GetBottomMargin(),
			      .7,
			      .3);
  l->SetFillStyle(0);
  l->SetBorderSize(0);
  c->Modified();
  c->Print(Form("plots/%s.png", h->GetName()));  
}

		 
void CompareFlgs(UShort_t    f1,
		 UShort_t    f2,
		 const char* var,
		 Bool_t      noTruth=true)
{
  TList* defs = GetHists(f1, var);
  TList* oths = GetHists(f2, var);
  
  TIter nextDef(defs);
  TIter nextOth(oths);
  THStack* result =  new THStack(Form("compare_%s", var),
				  Form("\\hbox{combinatorics vs. injection }"
				       "%s\\hbox{ %s}", obs, var));
  TH1* def = 0;
  TH1* oth = 0;
  while ((def = static_cast<TH1*>(nextDef())) &&
	 (oth = static_cast<TH1*>(nextOth()))) {
    if (noTruth && TString(def->GetName()).EqualTo("truth")) continue;
    def->SetName(f1 == 0x3 ? "combinatorics" : "injection"); 
    oth->SetName(f2 == 0x3 ? "combinatorics" : "injection");
    result->Add(Compare(def,oth));
  }
  TCanvas* c = new TCanvas(result->GetName(),result->GetTitle(), cW, cH);
  c->SetTopMargin(0.01);
  c->SetRightMargin(0.01); 
  c->SetTicks();
  result->Draw("nostack");
  result->GetHistogram()->SetMinimum(.9);
  result->GetHistogram()->SetMaximum(1.1);
  result->GetHistogram()->GetYaxis()->SetTitleOffset(1.5);
  result->GetHistogram()->SetXTitle("\\eta");
  result->GetHistogram()->SetYTitle(Form("%s / %s",
					 f2==0?"injection":"combinatorial",
					 f1==0?"injection":"combinatorial"));
  AddLine(result->GetHistogram());
  result->SetMinimum(.9);
  result->SetMaximum(1.1);
  c->Modified();
  c->Print(Form("plots/%s.png", result->GetName()));
}

void CompareSubs(UShort_t flgs, const char* var, Bool_t noTruth=false)
{
  TFile* file = TFile::Open(Form("results/combine_none_0x%x.root",flgs),"READ");
  if (!file) return;
  TH1* cent = GetH1(file, "cent");
  if (!cent) return;

  for (Int_t i = 1; i <= cent->GetNbinsX(); i++) {
    TString name;
    Double_t c1 = cent->GetXaxis()->GetBinLowEdge(i);
    Double_t c2 = cent->GetXaxis()->GetBinUpEdge (i);
    name.Form("cent%03d%02d_%03d%02d",
	      Int_t(c1), Int_t(c1*100)%100, Int_t(c2), Int_t(c2*100)%100);
    TString sname(name); sname.Append("/summary");
    TString pname(sname);
    pname.ReplaceAll("_", "");
    pname.ReplaceAll("cent","");
    pname.ReplaceAll("00", "");
    TList* defs = GetHists(flgs, "none", sname);
    TList* oths = GetHists(flgs, var,    sname);
    if (!defs || !oths) break;
    
    TIter nextDef(defs);
    TIter nextOth(oths);
    THStack* result = new THStack(Form("compare_%s_0x%x_%s",
				       var, flgs, name.Data()),
				  Form("\\hbox{%s vs. default }%s"
				       "\\hbox{ %s %s}",
				       var, obs, 
				       (flgs & 0x3) == 0x3 ?
				       "combinatorics" :
				       "injection",
				       pname.Data()));
    TH1* def = 0;
    TH1* oth = 0;
    while ((def = static_cast<TH1*>(nextDef())) &&
	   (oth = static_cast<TH1*>(nextOth()))) {
      if (noTruth && TString(def->GetName()).EqualTo("truth")) continue;
      result->Add(Compare(def,oth));
    }
    TCanvas* c = new TCanvas(result->GetName(),result->GetTitle(), cW, cH);
    c->SetTopMargin(0.01);
    c->SetRightMargin(0.01); 
    result->Draw("nostack");
    result->GetHistogram()->SetMinimum(.9);
    result->GetHistogram()->SetMaximum(1.1);
    result->GetHistogram()->GetYaxis()->SetTitleOffset(1.5);
    result->GetHistogram()->SetXTitle("\\eta");
    result->GetHistogram()->SetYTitle(Form("%s / default", var));
    result->SetMinimum(.9);
    result->SetMaximum(1.1);
    AddLine(result->GetHistogram());
    c->Modified();
    c->Print(Form("plots/%s.png", result->GetName()));
  }
}

void CentPlot(UShort_t flags)  
{
  TList* defs   = GetHists(flags, "none");
  TH1*   cent   = GetCent(flags, "none");
  // defs->ls();
  TCanvas* c = new TCanvas("legend", "Legend", cW, cH);
  c->SetTopMargin(0.01);
  c->SetRightMargin(0.01);
  TLegend* l = new TLegend(0.01, 0.01, .99, .99);
  l->SetBorderSize(0);
  l->SetFillStyle(0);
  // l->SetNColumns(2);
  TIter next(defs);
  TH1*  h = 0;
  Int_t bin = 1;
  while ((h = static_cast<TH1*>(next()))) {
    if (TString(h->GetName()).Contains("truth")) continue;
    TString nme; nme.Form("%2d - %3d%%",
			  Int_t(cent->GetXaxis()->GetBinLowEdge(bin)),
			  Int_t(cent->GetXaxis()->GetBinUpEdge(bin)));
    bin++;
    TLegendEntry* e = l->AddEntry("", nme, "F");
    e->SetFillColor(h->GetMarkerColor());
    e->SetFillStyle(1001);
  }
  c->cd();
  l->Draw();
  c->Modified();
  c->Print(Form("plots/%s.png", c->GetName()));
}  

  

  
void
Compare(const char* var)
{
  if (!gROOT->GetClass("GraphSysErr"))
    gROOT->LoadMacro("$HOME/GraphSysErr/GraphSysErr.C+g");
  gSystem->mkdir("plots",1);
  TString w(var);
  // CompareVars(0x0,var);
  if (!w.EqualTo("none")) CompareVars(0x3,var);
  // CompareFlgs(0x0,0x3,var);
  // CompareMids(0x0,var);
  CompareMids(0x3,var);
  if (w.EqualTo("none")) CentPlot(0x3);
  // CompareSubs(0x3,var);
}
