#include "ZebuMoonraker.hpp"
#include <cassert>
#include <iostream>
using namespace Slic3r::Zebu;
int main(){
  assert(normalize_base_url(" 192.168.1.89/ ")=="http://192.168.1.89");
  assert(normalize_base_url("https://x.y:4030")=="https://x.y:4030");
  assert(resolve_url("http://a:7125","/webcam/?action=snapshot")=="http://a:7125/webcam/?action=snapshot");
  std::string objs=R"({"result":{"objects":["webhooks","extruder","heater_bed","temperature_sensor Chamber_temp","heater_generic chamber_heater","print_stats"]}})";
  std::string ch=find_chamber_object(objs); std::cout<<"chamber="<<ch<<"\n"; assert(ch=="heater_generic chamber_heater");
  std::cout<<status_query_path(ch)<<"\n";
  std::string body=R"({"result":{"eventtime":1,"status":{"extruder":{"temperature":214.6,"target":215.0},"heater_bed":{"temperature":69.9,"target":70},
   "heater_generic chamber_heater":{"temperature":44.2,"target":45},"print_stats":{"state":"printing","filename":"cube.gcode","print_duration":1800,"message":"","info":{"current_layer":37,"total_layer":210}},
   "display_status":{"progress":0.25,"message":null},"virtual_sdcard":{"progress":0.3}}}})";
  auto s=parse_status(body,ch);
  std::cout<<s.ok<<" noz="<<s.nozzle<<"/"<<s.nozzle_target<<" bed="<<s.bed<<" ch="<<s.chamber<<"/"<<s.chamber_target<<" state="<<s.print_state<<" prog="<<s.progress<<" eta="<<format_duration(s.eta_seconds)<<" layer="<<s.layer<<"/"<<s.total_layers<<"\n";
  assert(s.ok && s.progress==0.25 && s.eta_seconds==5400 && s.layer==37);
  auto e=parse_status(R"({"error":{"code":503,"message":"Klippy Disconnected"}})","");
  std::cout<<"err="<<e.error<<"\n"; assert(!e.ok);
  std::cout<<snapshot_url(R"({"result":{"webcams":[{"name":"cam","enabled":true,"snapshot_url":"/webcam/?action=snapshot","stream_url":"/webcam/?action=stream"}]}})","http://g3d:4030")<<"\n";
  std::cout<<snapshot_url("garbage","http://g3d")<<"\n";
  std::cout<<"ALL OK\n";
}
