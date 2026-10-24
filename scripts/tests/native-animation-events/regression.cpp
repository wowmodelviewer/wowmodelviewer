#include "NativeAnimationEvents.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
using namespace nativeevents;
static int checks=0;
static void check(bool value) { ++checks; if(!value) throw std::runtime_error("assertion " + std::to_string(checks)); }
template<class T> static void put(Bytes & b,size_t o,T value) {
  if(b.size()<o+sizeof(T))b.resize(o+sizeof(T)); std::memcpy(b.data()+o,&value,sizeof(T));
}
static void u(Bytes & b,size_t o,uint32_t value) { put(b,o,value); }
static void h(Bytes & b,size_t o,uint16_t value) { put(b,o,value); }
static Bytes chunk(const std::string & tag,Bytes b) {
  Bytes out(tag.begin(),tag.end());u(out,4,(uint32_t)b.size());out.insert(out.end(),b.begin(),b.end());return out;
}
static void add(Bytes & f,const std::string & tag,Bytes b) {auto c=chunk(tag,b);f.insert(f.end(),c.begin(),c.end());}
static void array(Bytes & b,size_t at,uint32_t n,uint32_t offset) {u(b,at,n);u(b,at+4,offset);}
static void seq(Bytes & b,size_t at,int id,int sub=0,int flags=0x20,int alias=0) {
  h(b,at,id);h(b,at+2,sub);u(b,at+4,1000);u(b,at+12,flags);h(b,at+62,alias);
}
struct Fixture {
  std::map<uint32_t,Bytes> files;
  Bytes md,sk,parent;
  Fixture() : md(600),sk(192),parent(192) {
    std::memcpy(md.data(),"MD20",4);u(md,4,274);array(md,28,2,304);u(md,44,1);
    seq(md,304,53);seq(md,368,53,1);
    array(md,256,1,432);std::memcpy(md.data()+432,"$CSL",4);u(md,436,0xfedcba98);u(md,440,0);
    put<float>(md,444,1.25f);h(md,456,0);h(md,458,0xffff);array(md,460,2,480);
    array(md,480,1,520);array(md,488,0,0xffffffff);u(md,520,200);
  }
  void save() {files[1]=chunk("MD21",md);}
  Reader reader() {return Reader(1,[&](uint32_t id){auto it=files.find(id);require(it!=files.end(),"fixture file missing");return it->second;});}
  void skeleton(bool inherited=false) {
    array(sk,8,2,48);seq(sk,48,53);seq(sk,112,53,1);
    Bytes bones(16);u(bones,0,1);add(files[2],"SKS1",sk);add(files[2],"SKB1",bones);
    Bytes skid(4);u(skid,0,2);add(files[1],"SKID",skid);
    if(inherited) {
      // Child has different identities at the same local indices; parent 53 must
      // read PEDC. A valid but wrong child timestamp makes index confusion visible.
      array(parent,8,2,48);seq(parent,48,53);seq(parent,112,53,1);
      add(files[3],"SKS1",parent);add(files[3],"SKB1",bones);
      Bytes link(16);u(link,8,3);add(files[2],"SKPD",link);
      sk=Bytes(192);array(sk,8,2,48);seq(sk,48,0);seq(sk,112,1);
      files[2]=chunk("SKS1",sk);add(files[2],"SKB1",bones);add(files[2],"SKPD",link);
      Bytes p(80);array(p,0,1,16);h(p,18,0xffff);array(p,20,2,32);array(p,32,1,64);u(p,64,200);
      add(files[1],"PEDC",p);
    }
  }
};
template<class F> static void rejects(F fn) {bool failed=false;try{fn();}catch(const std::exception &){failed=true;}check(failed);}
static void synthetic() {
  Fixture f;f.save();auto c=f.reader().clip(53,0,1000);
  check(c.events.size()==1 && c.events[0].timestamps==std::vector<uint32_t>{200});
  check(c.events[0].event.payload==0xfedcba98 && c.events[0].event.position[0]==1.25f);
  check(f.reader().clip(53,1,1000).events.empty());
  u(f.md,520,0);std::memcpy(f.md.data()+432,"$DTH",4);f.save();c=f.reader().clip(53,0,1000);
  check(c.events[0].timestamps[0]==0 && c.events[0].event.code=="$DTH");
  u(f.md,520,200);f.save();
  // Alias keeps the selected sub-identity while resolving timestamp array zero.
  seq(f.md,368,53,1,0x40,0);f.save();c=f.reader().clip(53,1,1000);
  check(c.identity.sub==1 && c.resolved==0 && c.events[0].timestamps[0]==200);
  h(f.md,430,1);f.save();rejects([&]{f.reader().clip(53,1,1000);});
  f=Fixture();h(f.md,458,0);array(f.md,20,1,540);u(f.md,540,2000);array(f.md,460,1,480);u(f.md,520,1500);f.save();
  c=f.reader().clip(53,0,1000);check(c.events[0].global==0 && c.events[0].period==2000 && c.events[0].timestamps[0]==1500);
  u(f.md,520,2001);f.save();rejects([&]{f.reader().clip(53,0,1000);});
  h(f.md,458,1);f.save();rejects([&]{f.reader().clip(53,0,1000);});
  f=Fixture();seq(f.md,304,53,0,0);f.save();Bytes af(8);h(af,0,53);u(af,4,4);add(f.files[1],"AFID",af);
  Bytes afm(524);u(afm,520,321);f.files[4]=chunk("AFM2",afm);Bytes bone(524);u(bone,520,999);add(f.files[4],"AFSB",bone);
  c=f.reader().clip(53,0,1000);check(c.events[0].timestamps[0]==321 && c.events[0].externalFileId==4);
  f.files[4]=chunk("AFSB",bone);rejects([&]{f.reader().clip(53,0,1000);});
  f.files.erase(4);rejects([&]{f.reader().clip(53,0,1000);});
  f=Fixture();f.save();f.skeleton(true);c=f.reader().clip(53,0,1000);
  check(c.owner==3 && c.events[0].chunk=="PEDC" && c.events[0].timestamps[0]==200);
  // PEDC contains resident event keys even for externally stored parent bones.
  auto parentFile=File(f.files[3]);auto parentSks=parentFile.chunk("SKS1").bytes;
  seq(parentSks,48,53,0,0x80);f.files[3]=chunk("SKS1",parentSks);
  add(f.files[3],"SKB1",parentFile.chunk("SKB1").bytes);
  c=f.reader().clip(53,0,1000);check(c.events[0].externalFileId==0 && c.events[0].timestamps[0]==200);
  Bytes black(2);h(black,0,53);add(f.files[1],"PABC",black);rejects([&]{f.reader().clip(53,0,1000);});
  f=Fixture();f.save();f.skeleton();c=f.reader().clip(53,0,1000);check(c.owner==2 && c.events[0].chunk=="MD21");
  // Partial skeletons keep the parent's rig. Event indices must be checked
  // against that exported rig, not the child's discarded bone table.
  f=Fixture();u(f.md,440,1);f.save();f.skeleton(true);
  auto partialParent=File(f.files[3]);Bytes twoBones(16);u(twoBones,0,2);
  f.files[3]=chunk("SKS1",partialParent.chunk("SKS1").bytes);add(f.files[3],"SKB1",twoBones);
  c=f.reader().clip(53,0,1000);check(c.owner==3 && c.events[0].event.bone==1);
  f=Fixture();u(f.md,440,1);f.save();f.skeleton(true);
  auto partialChild=File(f.files[2]);f.files[2]=chunk("SKS1",partialChild.chunk("SKS1").bytes);
  add(f.files[2],"SKB1",twoBones);add(f.files[2],"SKPD",partialChild.chunk("SKPD").bytes);
  rejects([&]{f.reader();});
  // A partial intermediate parent does not define the effective rig either.
  // The child's own clips still overlay when it matches the retained ancestor.
  f=Fixture();f.save();f.skeleton(true);
  auto middle=File(f.files[3]);f.files[4]=chunk("SKS1",middle.chunk("SKS1").bytes);add(f.files[4],"SKB1",twoBones);
  Bytes grandparentLink(16);u(grandparentLink,8,4);add(f.files[3],"SKPD",grandparentLink);
  auto child=File(f.files[2]);auto childSequences=child.chunk("SKS1").bytes;seq(childSequences,48,53);
  f.files[2]=chunk("SKS1",childSequences);add(f.files[2],"SKB1",twoBones);add(f.files[2],"SKPD",child.chunk("SKPD").bytes);
  c=f.reader().clip(53,0,1000);check(c.owner==2 && c.events[0].chunk=="MD21");
  f.files[2]=chunk("SKS1",childSequences);add(f.files[2],"SKB1",child.chunk("SKB1").bytes);add(f.files[2],"SKPD",child.chunk("SKPD").bytes);
  rejects([&]{f.reader().clip(53,0,1000);}); // ancestor-owned events remain unsupported
  f=Fixture();f.save();f.skeleton(true);auto modelFile=File(f.files[1]);
  f.files[1]=chunk("MD21",modelFile.chunk("MD21").bytes);add(f.files[1],"SKID",modelFile.chunk("SKID").bytes);
  rejects([&]{f.reader().clip(53,0,1000);});
  f=Fixture();array(f.md,256,0,0);f.save();check(f.reader().clip(53,0,1000).events.empty());
  f=Fixture();u(f.md,520,1001);f.save();rejects([&]{f.reader().clip(53,0,1000);});
  f=Fixture();array(f.md,480,2,520);u(f.md,524,100);f.save();rejects([&]{f.reader().clip(53,0,1000);});
  f=Fixture();array(f.md,460,2,0xfffffff0);f.save();rejects([&]{f.reader().clip(53,0,1000);});
  f=Fixture();u(f.md,440,2);f.save();rejects([&]{f.reader();});
  f=Fixture();f.save();f.files[1].pop_back();rejects([&]{f.reader();});
  f=Fixture();f.save();rejects([&]{f.reader().clip(53,0,999);});
  std::cout<<"PASS "<<checks<<" native-event synthetic assertions\n";
}
int main(int argc,char ** argv) {
  try {
    if(argc==1) {synthetic();return 0;}
    if(argc==5 && std::string(argv[1])=="--all") {
      auto read=[&](uint32_t id){
        std::ifstream f(std::string(argv[3])+"/"+std::to_string(id)+".bin",std::ios::binary);
        require(f.good(),"missing corpus file "+std::to_string(id));return Bytes(std::istreambuf_iterator<char>(f),{});
      };
      uint32_t model=(uint32_t)std::stoul(argv[2]); File m(read(model));
      std::map<std::pair<int,int>,Sequence> identities; std::vector<uint32_t> ids;
      if(m.has("SKID")) {
        uint32_t id=m.chunk("SKID").get<uint32_t>(0);
        while(id) {require(ids.size()<32,"cycle");ids.push_back(id);File f(read(id));id=f.has("SKPD")?f.chunk("SKPD").get<uint32_t>(8):0;}
      } else ids.push_back(model);
      for(auto id:ids) {
        File f(read(id));Source s(id,f,id==model);
        for(auto q:s.sequences) if(q.id<=225) identities.emplace(std::make_pair(q.id,q.sub),q);
      }
      Reader reader(model,read);std::ofstream out(argv[4]);require(out.good(),"report write failed");
      size_t eventCount=0;
      for(auto identity:identities) {
        auto q=identity.second;auto c=reader.clip(q.id,q.sub,q.duration);
        out<<"C\t"<<q.id<<"\t"<<q.sub<<"\t"<<q.duration<<"\t"<<c.owner<<"\t"<<c.sequence<<"\t"<<c.resolved<<"\n";
        for(auto t:c.events) for(auto time:t.timestamps) {
          out<<"E\t"<<q.id<<"\t"<<q.sub<<"\t"<<t.event.index<<"\t"<<time<<"\t"<<t.event.payload<<"\t"<<t.event.bone<<"\t"<<t.global<<"\t"<<t.period<<"\t"<<t.chunk<<"\t"<<t.externalFileId<<"\n";++eventCount;
        }
      }
      std::cout<<"PASS corpus "<<model<<" clips="<<identities.size()<<" timestamps="<<eventCount<<"\n";return 0;
    }
    // Raw corpus CLI: model id, directory, animation id, sub-id, duration.
    require(argc==7,"usage: native-events model raw-folder anim sub duration report.tsv");
    Reader r((uint32_t)std::stoul(argv[1]),[&](uint32_t id){
      std::ifstream f(std::string(argv[2])+"/"+std::to_string(id)+".bin",std::ios::binary);
      require(f.good(),"missing corpus file "+std::to_string(id));return Bytes(std::istreambuf_iterator<char>(f),{});
    });
    auto c=r.clip((uint16_t)std::stoi(argv[3]),(uint16_t)std::stoi(argv[4]),(uint32_t)std::stoul(argv[5]));
    std::ofstream out(argv[6]);require(out.good(),"cannot write report");
    out<<"owner\t"<<c.owner<<"\tsequence\t"<<c.sequence<<"\tresolved\t"<<c.resolved<<"\n";
    for(auto t:c.events) for(auto time:t.timestamps)
      out<<t.event.index<<"\t"<<t.event.code<<"\t"<<time<<"\t"<<t.event.payload<<"\t"<<t.event.bone<<"\t"<<t.global<<"\t"<<t.period<<"\t"<<t.chunk<<"\t"<<t.externalFileId<<"\n";
    return 0;
  }catch(const std::exception & e){std::cerr<<e.what()<<"\n";return 1;}
}
