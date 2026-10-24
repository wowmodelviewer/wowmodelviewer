#ifndef WMV_NATIVE_ANIMATION_EVENTS_H
#define WMV_NATIVE_ANIMATION_EVENTS_H

// Timestamp-only M2 tracks. This reader deliberately does not share the bone
// animation buffer: chunked external event offsets address AFM2, never AFSB.
// All views own their bytes; reading cannot change an open GameFile's cursor.
#include <cstdint>
#include <cstring>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace nativeevents {
using Bytes = std::vector<unsigned char>;
inline void require(bool ok, const std::string & why) {
  if (!ok) throw std::runtime_error("Native animation events: " + why);
}
struct Buffer {
  Bytes bytes;
  void range(size_t offset, size_t count, size_t stride) const {
    require(offset <= bytes.size() && stride && count <= (bytes.size()-offset)/stride, "array outside source buffer");
  }
  template<class T> T get(size_t offset) const {
    range(offset, 1, sizeof(T)); T value; std::memcpy(&value, bytes.data()+offset, sizeof(T)); return value;
  }
  struct Array { uint32_t count, offset; };
  Array array(size_t at, size_t stride) const {
    Array a{get<uint32_t>(at), get<uint32_t>(at+4)};
    // Empty arrays can carry an unused offset into an external buffer.
    if (a.count) range(a.offset, a.count, stride);
    return a;
  }
};
struct File {
  std::map<std::string, Buffer> chunks;
  explicit File(const Bytes & raw) {
    require(raw.size() >= 8, "truncated file");
    if (!std::memcmp(raw.data(), "MD20", 4)) { chunks["MD21"].bytes = raw; return; }
    for (size_t at=0; at<raw.size();) {
      require(raw.size()-at >= 8, "truncated chunk header");
      uint32_t n; std::memcpy(&n, raw.data()+at+4, 4);
      require(n <= raw.size()-at-8, "truncated chunk payload");
      std::string tag(reinterpret_cast<const char *>(raw.data()+at),4);
      require(!chunks.count(tag), "duplicate chunk " + tag);
      chunks[tag].bytes.assign(raw.begin()+at+8, raw.begin()+at+8+n); at += 8+n;
    }
  }
  const Buffer & chunk(const std::string & tag) const {
    auto it=chunks.find(tag); require(it!=chunks.end(), "missing chunk " + tag); return it->second;
  }
  bool has(const std::string & tag) const { return chunks.count(tag)!=0; }
};
struct Sequence {
  uint16_t id, sub, alias;
  uint32_t duration, flags;
};
struct Source {
  uint32_t id;
  const File * file;
  std::vector<Sequence> sequences;
  std::vector<uint32_t> globals;
  std::map<std::pair<uint16_t,uint16_t>,uint32_t> external;
  size_t boneCount;
  Source(uint32_t fileId, const File & f, bool model) : id(fileId), file(&f) {
    const auto & b=f.chunk(model?"MD21":"SKS1");
    if (model) require(b.get<uint32_t>(4)==274, "unsupported M2 version (expected Retail layout 274)");
    auto a=b.array(model?28:8,64); std::set<std::pair<int,int>> identities;
    for (size_t i=0;i<a.count;++i) {
      size_t o=a.offset+i*64;
      Sequence s{b.get<uint16_t>(o),b.get<uint16_t>(o+2),b.get<uint16_t>(o+62),b.get<uint32_t>(o+4),b.get<uint32_t>(o+12)};
      require(identities.insert({s.id,s.sub}).second,"duplicate sequence identity"); sequences.push_back(s);
    }
    a=b.array(model?20:0,4);
    for(size_t i=0;i<a.count;++i) globals.push_back(b.get<uint32_t>(a.offset+4*i));
    boneCount=model?b.get<uint32_t>(44):f.chunk("SKB1").get<uint32_t>(0);
    if(f.has("AFID")) {
      const auto & af=f.chunk("AFID"); require(af.bytes.size()%8==0,"invalid AFID size");
      for(size_t o=0;o<af.bytes.size();o+=8) {
        auto key=std::make_pair(af.get<uint16_t>(o),af.get<uint16_t>(o+2));
        require(!external.count(key),"duplicate AFID identity"); external[key]=af.get<uint32_t>(o+4);
      }
    }
  }
  uint32_t externalId(size_t index) const {
    const auto & s=sequences.at(index); auto it=external.find({s.id,s.sub}); return it==external.end()?0:it->second;
  }
  size_t resolve(size_t index) const {
    std::set<size_t> seen;
    for (;;) {
      require(index<sequences.size() && seen.insert(index).second,"invalid or cyclic sequence alias");
      auto s=sequences[index];
      // Match the loader's sequence-source precedence (resident/external before alias).
      if((s.flags&0x20) || externalId(index) || !(s.flags&0x40)) return index;
      index=s.alias;
    }
  }
};
struct Event {
  size_t index;
  std::string code;
  uint32_t payload, bone;
  float position[3];
};
struct Track {
  Event event;
  uint16_t interpolation;
  int16_t global;
  uint32_t period=0, externalFileId=0;
  std::string chunk;
  std::vector<uint32_t> timestamps;
};
struct Clip {
  uint32_t owner;
  size_t sequence, resolved;
  Sequence identity;
  std::vector<Track> events;
};
class Reader {
  std::function<Bytes(uint32_t)> fetch;
  std::map<uint32_t,File> files;
  uint32_t modelId;
  std::vector<Source> sources;
  std::vector<Event> definitions;
  std::set<uint16_t> blacklist;
  const File & file(uint32_t id) {
    auto it=files.find(id);
    if(it==files.end()) it=files.emplace(id,File(fetch(id))).first;
    return it->second;
  }
public:
  Reader(uint32_t id, std::function<Bytes(uint32_t)> read) : fetch(read), modelId(id) {
    const auto & m=file(id); const auto & b=m.chunk("MD21");
    require(b.get<uint32_t>(4)==274,"unsupported M2 event layout (expected Retail layout 274)");
    if(m.has("SKID")) {
      uint32_t sid=m.chunk("SKID").get<uint32_t>(0); std::set<uint32_t> seen;
      while(sid) {
        require(seen.size()<32 && seen.insert(sid).second,"skeleton inheritance cycle");
        const auto & s=file(sid); sources.emplace_back(sid,s,false);
        sid=s.has("SKPD")?s.chunk("SKPD").get<uint32_t>(8):0;
      }
      require(!sources.empty(),"empty skeleton reference");
    } else sources.emplace_back(id,m,true);
    if(m.has("PABC")) {
      const auto & p=m.chunk("PABC"); require(p.bytes.size()%2==0,"invalid PABC size");
      for(size_t o=0;o<p.bytes.size();o+=2) blacklist.insert(p.get<uint16_t>(o));
    }
    auto a=b.array(256,36);
    for(size_t i=0;i<a.count;++i) {
      size_t o=a.offset+i*36; Event e;
      e.index=i; e.code.assign(reinterpret_cast<const char *>(b.bytes.data()+o),4);
      e.payload=b.get<uint32_t>(o+4); e.bone=b.get<uint32_t>(o+8);
      // loadSkeleton starts with the ancestor's rig and only overlays children
      // with the same bone count. A partial child's table is not exported.
      require(e.bone==UINT32_MAX || e.bone<sources.back().boneCount,"invalid event bone reference");
      for(int c=0;c<3;++c) { e.position[c]=b.get<float>(o+12+c*4); require(std::isfinite(e.position[c]),"non-finite event position"); }
      definitions.push_back(e);
    }
    if(m.has("PEDC")) require(m.chunk("PEDC").array(0,12).count==a.count,"PEDC event count mismatch");
  }
  const std::vector<Event> & eventDefinitions() const { return definitions; }
  Clip clip(uint16_t id, uint16_t sub, uint32_t duration) {
    size_t owner=sources.size(), local=0;
    for(size_t i=0;i<sources.size() && owner==sources.size();++i) {
      // The existing bone loader retains the parent's rig for partial skeletons.
      if(i+1<sources.size() && (sources[i].boneCount!=sources.back().boneCount || sources[i].sequences.empty())) continue;
      if(i && blacklist.count(id)) continue;
      for(size_t j=0;j<sources[i].sequences.size();++j)
        if(sources[i].sequences[j].id==id && sources[i].sequences[j].sub==sub) { owner=i; local=j; break; }
    }
    require(owner<sources.size(),"selected identity missing or parent-blacklisted");
    const auto & s=sources[owner]; const auto selected=s.sequences[local];
    require(selected.duration==duration,"exported/source duration mismatch");
    const size_t key=s.resolve(local); const auto resolved=s.sequences[key];
    require(resolved.duration==duration,"alias duration differs from exported clip");
    Clip result{s.id,local,key,selected,{}};
    require(owner<=1,"event inheritance deeper than PEDC's immediate parent is unsupported");
    const auto & m=file(modelId); const auto & md=m.chunk("MD21");
    auto events=md.array(256,36);
    const Buffer * tracks=&md; size_t base=events.offset+24, stride=36;
    if(owner) {
      require(m.has("PEDC") || definitions.empty(),"inherited events require PEDC");
      if(!definitions.empty()) { tracks=&m.chunk("PEDC"); base=tracks->array(0,12).offset; stride=12; }
    }
    for(const auto & e:definitions) {
      size_t o=base+e.index*stride; Track t; t.event=e; t.chunk=owner?"PEDC":"MD21";
      t.interpolation=tracks->get<uint16_t>(o); t.global=tracks->get<int16_t>(o+2);
      require(t.global>=-1,"invalid global sequence reference");
      auto outer=tracks->array(o+4,8); size_t entry=key;
      if(t.global>=0) {
        require((size_t)t.global<s.globals.size(),"global sequence outside owner table");
        t.period=s.globals[t.global]; require(t.period>0,"zero global period");
        require(outer.count<=1,"global event track has multiple timestamp arrays"); entry=0;
      } else require(outer.count<=s.sequences.size(),"event sequence table exceeds owner sequence count");
      if(entry>=outer.count) continue;
      size_t a=outer.offset+entry*8; uint32_t count=tracks->get<uint32_t>(a), offset=tracks->get<uint32_t>(a+4);
      if(!count) continue;
      const Buffer * keys=tracks;
      // PEDC is a self-contained parent-event replacement, including timestamps
      // for parent clips whose BONE animation lives in an external file. Those
      // resident PEDC offsets must never address the parent's AFM2.
      if(!owner && t.global<0 && !(resolved.flags&0x130)) {
        t.externalFileId=s.externalId(key);
        // Model AFID can supply event data independently of skeleton bone AFID.
        if(m.has("AFID")) {
          const auto & af=m.chunk("AFID"); require(af.bytes.size()%8==0,"invalid model AFID");
          uint32_t modelExternal=0;
          for(size_t p=0;p<af.bytes.size();p+=8)
            if(af.get<uint16_t>(p)==resolved.id && af.get<uint16_t>(p+2)==resolved.sub) {
              require(!modelExternal,"duplicate model AFID identity"); modelExternal=af.get<uint32_t>(p+4);
            }
          if(!owner && modelExternal) t.externalFileId=modelExternal;
        }
        require(t.externalFileId!=0,"missing external event animation reference");
        keys=&file(t.externalFileId).chunk("AFM2");
      }
      keys->range(offset,count,4); uint32_t previous=0;
      for(size_t i=0;i<count;++i) {
        uint32_t time=keys->get<uint32_t>(offset+i*4);
        require(time<=(t.global<0?duration:t.period) && (!i || time>=previous),"timestamp outside clock or out of order");
        t.timestamps.push_back(time); previous=time;
      }
      result.events.push_back(t);
    }
    return result;
  }
};
}
#endif
