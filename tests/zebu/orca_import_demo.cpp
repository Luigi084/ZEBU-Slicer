#include "ZebuOrcaImport.hpp"
#include <iostream>
using namespace Slic3r::Zebu;
int main(int argc,char**argv){
  auto scan = scan_orca(argv[1]);
  std::cout<<"indexed system presets: "<<scan.system_presets_indexed<<"\n";
  for (auto k:{PresetKind::Machine,PresetKind::Filament,PresetKind::Process})
    for(auto&p:scan.list(k)){ std::cout<<kind_folder(k)<<" | "<<p.name<<" | err="<<p.error<<" | keys="<<p.flat.size()<<" | suggest=";
      for(auto&s:p.suggested_printers) std::cout<<s<<";"; std::cout<<"\n";}
  for(auto&w:scan.warnings) std::cout<<"W: "<<w<<"\n";
  ImportPlan plan; plan.machines={"G3D Pro 360","Raise K2Plus","Broken"};
  plan.filament_bindings["PETG G3D @G3D Pro 360"]={"G3D Pro 360"};
  plan.filament_bindings["ABS maison"]={"Raise K2Plus","G3D Pro 360"};
  plan.process_bindings["0.20 G3D"]={"G3D Pro 360"};
  auto r = write_import(scan, plan, argv[2]);
  for(auto&s:r.written) std::cout<<"written "<<s<<"\n";
  for(auto&s:r.errors) std::cout<<"error "<<s<<"\n";
  auto r2 = write_import(scan, plan, argv[2]);
  std::cout<<"second run skipped="<<r2.skipped.size()<<" written="<<r2.written.size()<<"\n";
}
