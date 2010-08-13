#ifndef CFG_OPTIONS_H
#define CFG_OPTIONS_H

#include "hg_cfg.h"
#include "cfg_format.h"
#include "cfg_binarize.h"
//#include "program_options.h"

struct CFGOptions {
  CFGFormat format;
  CFGBinarize binarize;
  std::string cfg_output,source_cfg_output,cfg_unbin_output;
  void set_defaults() {
    format.set_defaults();
    binarize.set_defaults();
    cfg_output="";
  }
  CFGOptions() { set_defaults(); }
  template <class Opts> // template to support both printable_opts and boost nonprintable
  void AddOptions(Opts *opts) {
    opts->add_options()
      ("cfg_output", defaulted_value(&cfg_output),"write final target CFG (before FSA rescoring) to this file")
      ("source_cfg_output", defaulted_value(&source_cfg_output),"write source CFG (after prelm-scoring, prelm-prune) to this file")
      ("cfg_unbin_output", defaulted_value(&cfg_unbin_output),"write pre-binarization CFG to this file") //TODO:
    ;
    binarize.AddOptions(opts);
    format.AddOptions(opts);
  }
  void Validate() {
    format.Validate();
    binarize.Validate();
//    if (cfg_output.empty()) binarize.bin_name_nts=false;
  }
  char const* description() const {
    return "CFG output options";
  }
  void maybe_output_source(Hypergraph const& hg) {
    if (source_cfg_output.empty()) return;
    std::cerr<<"Printing source CFG to "<<source_cfg_output<<": "<<format<<'\n';
    WriteFile o(source_cfg_output);
    CFG cfg(hg,false,format.features,format.goal_nt());
    cfg.Print(o.get(),format);
  }
  void maybe_output(HgCFG &hgcfg) {
    if (cfg_output.empty()) return;
    std::cerr<<"Printing target CFG to "<<cfg_output<<": "<<format<<'\n';
    WriteFile o(cfg_output);
    maybe_binarize(hgcfg);
    hgcfg.GetCFG().Print(o.get(),format);
  }
  void maybe_binarize(HgCFG &hgcfg) {
    if (hgcfg.binarized) return;
    hgcfg.GetCFG().Binarize(binarize);
    hgcfg.binarized=true;
  }
};


#endif
