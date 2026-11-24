#include "ZebuUpdate.hpp"
#include <cassert>
#include <iostream>
using namespace Slic3r::Zebu;
int main(){
  assert(compare_versions("0.1.0-beta","0.1.0")<0);
  assert(compare_versions("v0.1.1-beta","0.1.0")>0);
  assert(compare_versions("0.1.0-beta","0.1.0-beta.2")<0);
  assert(compare_versions("0.1.0-beta.2","0.1.0-beta.10")<0);
  assert(compare_versions("0.1.0-beta","0.1.0-rc")<0);
  assert(compare_versions("0.2.0","0.10.0")<0);
  assert(compare_versions("0.1.0-beta","0.1.0-beta")==0);
  std::string rel=R"([
   {"tag_name":"v0.2.0-beta","draft":true,"prerelease":true,"assets":[]},
   {"tag_name":"v0.1.1-beta","name":"Beta 0.1.1","body":"fixes","html_url":"https://github.com/x/y/releases/tag/v0.1.1-beta","prerelease":true,
    "assets":[{"name":"ZEBU_Slicer_Windows_Installer_V0.1.1-beta_x64.exe","browser_download_url":"https://e/x.exe"},
              {"name":"ZEBU_Slicer_Windows_Installer_V0.1.1-beta_x64.exe.sha256","browser_download_url":"https://e/x.exe.sha256"},
              {"name":"ZEBU_Slicer_Linux_V0.1.1-beta.AppImage","browser_download_url":"https://e/x.AppImage"}]},
   {"tag_name":"v0.1.0-beta","prerelease":true,"assets":[]}])";
  auto r=pick_update(rel,"0.1.0-beta",UpdatePlatform::Windows,true);
  std::cout<<r.found<<" "<<r.version<<" "<<r.asset_name<<" sha="<<r.sha256_url<<"\n";
  assert(r.found && r.version=="0.1.1-beta" && !r.sha256_url.empty());
  auto l=pick_update(rel,"0.1.0-beta",UpdatePlatform::LinuxAppImage,true); assert(l.asset_name=="ZEBU_Slicer_Linux_V0.1.1-beta.AppImage" && l.sha256_url.empty());
  auto n=pick_update(rel,"0.1.0-beta",UpdatePlatform::Windows,false); assert(!n.found);
  auto u=pick_update(rel,"0.1.1-beta",UpdatePlatform::Windows,true); assert(!u.found);
  assert(parse_sha256_file("ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789  f.exe\n")=="abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
  assert(parse_sha256_file("bad").empty());
  std::cout<<"ALL OK\n";
}
