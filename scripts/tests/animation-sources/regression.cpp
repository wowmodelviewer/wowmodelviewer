#include "AnimationFileSnapshot.h"
#include "CASCChunks.h"
#include "animated.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <fbxsdk.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

static void require(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
class Fixture : public GameFile {
public:
  std::vector<unsigned char> bytes;
  bool opened = false, fail = false;
  int closes = 0;
  Fixture(std::vector<unsigned char> b)
      : GameFile("fixture"), bytes(std::move(b)) {}
  bool openFile() override {
    if (fail)
      return false;
    opened = true;
    return true;
  }
  bool isAlreadyOpened() override { return opened; }
  bool getFileSize(unsigned long long &s) override {
    s = bytes.size();
    return true;
  }
  unsigned long readFile() override {
    memcpy(buffer, bytes.data(), bytes.size());
    return (unsigned long)bytes.size();
  }
  void doPostOpenOperation() override {
    if (bytes.size() < 8 ||
        (memcmp(bytes.data(), "AFSB", 4) && memcmp(bytes.data(), "AFM2", 4) &&
         memcmp(bytes.data(), "AFSA", 4) && memcmp(bytes.data(), "SKB1", 4)))
      return;
    for (size_t at = 0; at + 8 <= bytes.size();) {
      uint32_t n;
      memcpy(&n, bytes.data() + at + 4, 4);
      Chunk c;
      c.magic = std::string((char *)bytes.data() + at, 4);
      c.start = (unsigned int)at + 8;
      c.size = n;
      c.pointer = 0;
      chunks.push_back(c);
      if (n > bytes.size() - at - 8)
        break;
      at += 8 + n;
    }
  }
  bool doPostCloseOperation() override {
    opened = false;
    ++closes;
    return true;
  }
};
static std::vector<unsigned char> read(const std::string &p) {
  std::ifstream f(p, std::ios::binary);
  require(bool(f), "missing raw fixture");
  return {std::istreambuf_iterator<char>(f), {}};
}
static std::vector<unsigned char> chunk(const std::vector<unsigned char> &b,
                                        const char *tag) {
  for (size_t at = 0; at + 8 <= b.size();) {
    uint32_t n;
    memcpy(&n, b.data() + at + 4, 4);
    require(n <= b.size() - at - 8, "bad chunk");
    if (!memcmp(b.data() + at, tag, 4))
      return {b.begin() + at + 8, b.begin() + at + 8 + n};
    at += 8 + n;
  }
  throw std::runtime_error("missing chunk");
}

// Test oracle: read the diagnosed source sequence/file directly, without the
// production alias resolver, snapshot loader, Animated::init or its sampler.
template <class T>
static T rawValue(const std::vector<unsigned char> &bytes, size_t offset) {
  require(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset,
          "oracle read outside source");
  T value;
  memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}
template <class T> struct RawTrack {
  std::vector<uint32_t> times;
  std::vector<T> values;
  int interpolation;
  uint32_t period = 0;
  T sample(uint32_t t) const {
    require(!values.empty(), "oracle sample of empty track");
    if (period)
      t %= period;
    if (t <= times.front())
      return values.front();
    if (t >= times.back())
      return values.back();
    const size_t right =
        std::upper_bound(times.begin(), times.end(), t) - times.begin();
    const size_t left = right - 1;
    if (!interpolation)
      return values[left];
    const float weight =
        float(t - times[left]) / float(times[right] - times[left]);
    return blend(values[left], values[right], weight);
  }
  static T blend(const T &a, const T &b, float w) {
    return a * (1 - w) + b * w;
  }
};
template <>
glm::fquat RawTrack<glm::fquat>::blend(const glm::fquat &a, const glm::fquat &b,
                                       float w) {
  return glm::slerp(a, b, w);
}
template <class T, class D, class Decode>
static RawTrack<T> rawTrack(const AnimationBlock &b, int source,
                            const std::vector<unsigned char> &headers,
                            const std::vector<unsigned char> &external,
                            const std::vector<uint32_t> &globals,
                            uint32_t duration, Decode decode) {
  RawTrack<T> result;
  result.interpolation = b.type;
  require(b.type == 0 || b.type == 1, "unsupported oracle interpolation");
  int index = b.seq >= 0 ? 0 : source;
  if (index >= b.nTimes || index >= b.nKeys)
    return result;
  const auto th =
      rawValue<AnimationBlockHeader>(headers, b.ofsTimes + 8 * index);
  const auto kh =
      rawValue<AnimationBlockHeader>(headers, b.ofsKeys + 8 * index);
  require(th.nEntrys == kh.nEntrys, "oracle mismatched key/time counts");
  const auto &bytes = b.seq >= 0 ? headers : external;
  if (b.seq >= 0) {
    require(b.seq < globals.size() && globals[b.seq] > 0,
            "oracle global period");
    result.period = globals[b.seq];
  }
  for (size_t i = 0; i < th.nEntrys; ++i) {
    result.times.push_back(rawValue<uint32_t>(bytes, th.ofsEntrys + i * 4));
    result.values.push_back(
        decode(rawValue<D>(bytes, kh.ofsEntrys + i * sizeof(D))));
  }
  require(std::is_sorted(result.times.begin(), result.times.end()),
          "oracle descending times");
  if (!result.times.empty())
    require(result.times.back() <= (result.period ? result.period : duration),
            "oracle keys exceed sequence duration");
  return result;
}
static void synthetic() {
  modelAnimData d;
  d.sequences.resize(8);
  d.sequences[0].animID = 69;
  d.sequences[1].animID = 69;
  d.sequences[1].subAnimID = 1;
  d.animfiles[0] = {1};
  d.animfiles[1] = {2};
  require(d.keySequence(0) == 0 && d.keySequence(1) == 1, "variant collision");
  d.sequences[2].flags = 0x40;
  d.sequences[2].Index = 3;
  d.sequences[3].flags = 0x40;
  d.sequences[3].Index = 1;
  require(d.keySequence(2) == 1, "external alias chain");
  d.sequences[4].flags = 0x20 | 0x40;
  d.sequences[4].Index = 99;
  require(d.keySequence(4) == 4, "internal primary wins");
  d.sequences[5].flags = 0x40;
  d.sequences[5].Index = 6;
  d.sequences[6].flags = 0x40;
  d.sequences[6].Index = 5;
  require(d.keySequence(5) == -1, "cycle accepted");
  d.sequences[6].Index = 99;
  require(d.keySequence(5) == -1, "bad alias accepted");
  Fixture own({1, 2, 3});
  std::vector<unsigned char> bytes;
  require(readAnimationSnapshot(own, bytes), "snapshot failed");
  require(!own.opened && own.closes == 1 && bytes[1] == 2, "owned file leaked");
  own.open();
  own.seek(1);
  auto ptr = own.getBuffer();
  require(readAnimationSnapshot(own, bytes), "borrow failed");
  require(own.opened && own.closes == 1 && own.getBuffer() == ptr &&
              own.getPos() == 1,
          "borrow modified owner");
  own.close();
  Fixture failure({});
  failure.fail = true;
  require(!readAnimationSnapshot(failure, bytes) && failure.closes == 0,
          "failed open closed borrowed file");
  Fixture chunks({'A', 'F', 'S', 'B', 3, 0, 0, 0, 7, 8, 9});
  require(readAnimationSnapshot(chunks, bytes) && bytes.size() == 3 &&
              bytes[0] == 7 && !chunks.opened,
          "AFSB snapshot");
  // Global track entry 0 must use internal bytes even when sequence 0 has an
  // external file.
  std::vector<unsigned char> internal(64);
  AnimationBlockHeader h{1, 40};
  memcpy(internal.data(), &h, 8);
  h.ofsEntrys = 44;
  memcpy(internal.data() + 8, &h, 8);
  uint32_t t = 0;
  float v = 7;
  memcpy(internal.data() + 40, &t, 4);
  memcpy(internal.data() + 44, &v, 4);
  Fixture f(internal);
  f.open();
  AnimationBlock b{};
  b.type = 1;
  b.seq = 0;
  b.nTimes = b.nKeys = 1;
  b.ofsTimes = 0;
  b.ofsKeys = 8;
  d.globalSequences = {1000};
  auto a = std::make_unique<Animated<float>>();
  a->init(b, f, d);
  require(a->data[0].size() == 1 && a->data[0][0] == 7,
          "global track read external buffer");
  f.close();
  globalTime = 321;
  require(a->getValueAtGlobalTime(0, 0, 777) == 7 && globalTime == 321,
          "export changed viewer clock");
  a->times[0] = {0, 1000};
  a->data[0] = {0, 10};
  require(a->getValueAtGlobalTime(0, 0, 500) == 5 &&
              a->getValueAtGlobalTime(0, 0, 1500) == 5 && globalTime == 321,
          "export clock froze or failed to wrap global motion");
  // Internal/external alias targets must select the same sequence for both
  // headers and payloads, even when the requested slot has different headers.
  modelAnimData sources;
  sources.sequences.resize(4);
  sources.sequences[0].animID = sources.sequences[1].animID = 69;
  sources.sequences[1].subAnimID = 1;
  sources.sequences[2].flags = 0x40;
  sources.sequences[2].Index = 1;
  sources.sequences[3].flags = 0x20;
  std::vector<unsigned char> headers(96);
  for (int i = 0; i < 4; ++i) {
    AnimationBlockHeader th{1, 80}, kh{1, 84};
    memcpy(headers.data() + i * 8, &th, 8);
    memcpy(headers.data() + 32 + i * 8, &kh, 8);
  }
  AnimationBlockHeader aliasTimes{1, 88}, aliasKeys{1, 92};
  memcpy(headers.data() + 16, &aliasTimes, 8);
  memcpy(headers.data() + 48, &aliasKeys, 8);
  auto payload = [&](float value) {
    auto result = headers;
    memcpy(result.data() + 84, &value, sizeof(value));
    return result;
  };
  sources.animfiles[0] = payload(11);
  sources.animfiles[1] = payload(22);
  Fixture aliasFile(payload(33));
  aliasFile.open();
  AnimationBlock ab{};
  ab.type = 1;
  ab.seq = -1;
  ab.nTimes = ab.nKeys = 4;
  ab.ofsKeys = 32;
  auto aliases = std::make_unique<Animated<float>>();
  aliases->init(ab, aliasFile, sources);
  require(aliases->getValue(0, 0) == 11 && aliases->getValue(1, 0) == 22 &&
              aliases->getValue(2, 0) == 22 && aliases->getValue(3, 0) == 33,
          "variant/alias/internal keys differ");
  sources.animfiles[1].resize(86); // complete timestamp, truncated float key
  auto truncated = std::make_unique<Animated<float>>();
  truncated->init(ab, aliasFile, sources);
  require(truncated->times[1].empty() && truncated->data[1].empty() &&
              truncated->times[2].empty() && truncated->data[2].empty(),
          "truncated track left unpaired timestamps");
  for (int mode : {INTERPOLATION_HERMITE, INTERPOLATION_BEZIER}) {
    sources.animfiles[1] = payload(22);
    float tangents[] = {4, 5};
    memcpy(sources.animfiles[1].data() + 88, tangents, sizeof(tangents));
    ab.type = mode;
    auto nonlinear = std::make_unique<Animated<float>>();
    nonlinear->init(ab, aliasFile, sources);
    require(nonlinear->data[2].size() == 1 && nonlinear->in[2].size() == 1 &&
                nonlinear->out[2].size() == 1 && nonlinear->data[2][0] == 22 &&
                nonlinear->in[2][0] == 4 && nonlinear->out[2][0] == 5,
            "nonlinear key stride");
  }
  aliasFile.close();
  Fixture single({'A', 'F', 'M', '2', 3, 0, 0, 0, 4, 5, 6});
  require(readAnimationSnapshot(single, bytes) &&
              bytes == std::vector<unsigned char>({4, 5, 6}) && !single.opened,
          "legacy single chunk");
  Fixture multi(
      {'A', 'F', 'M', '2', 1, 0, 0, 0, 4, 'A', 'F', 'S', 'A', 1, 0, 0, 0, 5});
  require(readAnimationSnapshot(multi, bytes) && bytes == multi.bytes,
          "legacy whole file");
  multi.open();
  require(multi.setChunk("AFSA"), "borrowed chunk selection");
  multi.seek(1);
  auto chunkPointer = multi.getBuffer();
  require(readAnimationSnapshot(multi, bytes) && bytes == multi.bytes,
          "borrowed chunk truncated the raw file");
  require(multi.opened && multi.closes == 1 &&
              multi.getBuffer() == chunkPointer && multi.getPos() == 1,
          "borrowed chunk state changed");
  multi.close();
  Fixture invalid(
      {'A', 'F', 'M', '2', 1, 0, 0, 0, 4, 'A', 'F', 'S', 'A', 99, 0, 0, 0, 5});
  require(!readAnimationSnapshot(invalid, bytes) && !invalid.opened &&
              invalid.closes == 1,
          "invalid snapshot leaked");
  d.sequences[3].Index = 4;
  require(d.keySequence(2) == 4, "internal alias chain");
  d.animfiles[0].clear();
  b.seq = -1;
  auto missing = std::make_unique<Animated<float>>();
  f.open();
  missing->init(b, f, d);
  require(missing->data[0].empty(), "missing external fell back to internal");
  f.close();
  std::cout << "synthetic variants/alias "
               "chains/cycles/primary/global/owned/borrowed/chunk PASS\n";
}
static void collect(FbxNode *n, std::vector<FbxNode *> &bones) {
  if (n->GetSkeleton())
    bones.push_back(n);
  for (int i = 0; i < n->GetChildCount(); ++i)
    collect(n->GetChild(i), bones);
}
int main(int argc, char **argv) {
  try {
    synthetic();
    if (argc == 1)
      return 0;
    require(
        argc == 4,
        "usage: animation-sources [raw-directory input.fbx comparison.json]");
    std::string raw = argv[1];
    auto skel = read(raw + "/2138400.bin"), sks = chunk(skel, "SKS1"),
         skb = chunk(skel, "SKB1"), afid = chunk(skel, "AFID");
    SKS1 header;
    memcpy(&header, sks.data(), sizeof(header));
    modelAnimData d;
    d.globalSequences.resize(header.nGlobalSequences);
    memcpy(d.globalSequences.data(), sks.data() + header.ofsGlobalSequences,
           d.globalSequences.size() * 4);
    d.sequences.resize(header.nAnimations);
    memcpy(d.sequences.data(), sks.data() + header.ofsAnimations,
           d.sequences.size() * sizeof(ModelAnimation));
    for (size_t i = 0; i < d.sequences.size(); ++i)
      for (size_t at = 0; at + 8 <= afid.size(); at += 8) {
        AFID e;
        memcpy(&e, afid.data() + at, 8);
        if (e.fileId && e.animId == d.sequences[i].animID &&
            e.subAnimId == d.sequences[i].subAnimID) {
          std::string path = raw + "/" + std::to_string(e.fileId) + ".bin";
          d.animfiles[i] = {};
          if (!std::ifstream(path))
            break;
          Fixture f(read(path));
          require(readAnimationSnapshot(f, d.animfiles[i]),
                  "raw snapshot failed");
          require(!f.opened, "raw file leaked");
          break;
        }
      }
    SKB1 bh;
    memcpy(&bh, skb.data(), sizeof(bh));
    std::vector<ModelBoneDef> defs(bh.nBones);
    memcpy(defs.data(), skb.data() + bh.ofsBones,
           defs.size() * sizeof(ModelBoneDef));
    const int indices[] = {44,  48,  67,  68,  72,  78,  113,
                           124, 139, 140, 141, 142, 143, 155};
    const int targets[] = {44,  48,  67,  67,  72,  78,  113,
                           124, 139, 140, 141, 142, 143, 155};
    const int files[] = {1013009, 1013026, 1013001, 1013001, 1013020,
                         1012989, 1012993, 1013032, 1012998, 1013002,
                         1013035, 1013014, 1013036, 1013016};
    const char *names[] = {
        "EmoteTalk",        "EmotePoint",       "UseStandingLoop",
        "EmoteUseStanding", "EmoteWork",        "SitGround",
        "EmoteSalute",      "EmoteEat",         "EmoteDance",
        "EmoteDance_1",     "EmoteDance_2",     "EmoteDance_3",
        "EmoteDance_4",     "EmoteDanceSpecial"};
    FbxManager *mgr = FbxManager::Create();
    mgr->SetIOSettings(FbxIOSettings::Create(mgr, IOSROOT));
    auto importer = FbxImporter::Create(mgr, "");
    require(importer->Initialize(argv[2], -1, mgr->GetIOSettings()),
            "FBX initialize");
    auto scene = FbxScene::Create(mgr, "");
    require(importer->Import(scene), "FBX import");
    std::vector<FbxNode *> nodes;
    collect(scene->GetRootNode(), nodes);
    require(nodes.size() == defs.size(), "bone count mismatch");
    require(scene->GetSrcObjectCount<FbxAnimStack>() == 14,
            "unexpected take count");
    QJsonArray results;
    int totalErrors = 0;
    for (int clip = 0; clip < 14; ++clip) {
      int idx = indices[clip];
      const auto sourceKeys =
          chunk(read(raw + "/" + std::to_string(files[clip]) + ".bin"), "AFSB");
      require(d.keySequence(idx) == targets[clip], "wrong raw target");
      auto stack = scene->GetSrcObject<FbxAnimStack>(clip);
      require(stack != nullptr, "missing take");
      require(!strcmp(stack->GetName(), names[clip]), "wrong clip name/order");
      scene->SetCurrentAnimationStack(stack);
      scene->GetAnimationEvaluator()->Reset();
      double duration = d.sequences[idx].length;
      require(
          std::abs(stack->GetLocalTimeSpan().GetDuration().GetSecondDouble() *
                       1000 -
                   duration) < 0.01,
          "wrong duration");
      auto layer = stack->GetMember<FbxAnimLayer>();
      int errors = 0, values = 0, nonfinite = 0, moving = 0;
      double maxTrans = 0, maxScale = 0, maxRot = 0;
      for (size_t bone = 0; bone < defs.size(); ++bone) {
        auto vectorKey = [](glm::vec3 v) {
          require(std::isfinite(v.x) && std::isfinite(v.y) &&
                      std::isfinite(v.z),
                  "oracle nonfinite vector key");
          return v;
        };
        auto quaternionKey = [](PACK_QUATERNION v) {
          auto component = [](int16_t s) {
            return float(s < 0 ? s + 32768 : s - 32767) / 32767.0f;
          };
          return glm::fquat(component(v.w), component(v.x), component(v.y),
                            component(v.z));
        };
        auto rawT = rawTrack<glm::vec3, glm::vec3>(
            defs[bone].translation, targets[clip], skb, sourceKeys,
            d.globalSequences, duration, vectorKey);
        auto rawS = rawTrack<glm::vec3, glm::vec3>(
            defs[bone].scaling, targets[clip], skb, sourceKeys,
            d.globalSequences, duration, vectorKey);
        auto rawR = rawTrack<glm::fquat, PACK_QUATERNION>(
            defs[bone].rotation, targets[clip], skb, sourceKeys,
            d.globalSequences, duration, quaternionKey);
        FbxNode *node = nullptr;
        std::string suffix = "_bone_" + std::to_string(bone);
        for (auto n : nodes) {
          std::string name = n->GetName();
          if (name.size() >= suffix.size() &&
              name.substr(name.size() - suffix.size()) == suffix) {
            node = n;
            break;
          }
        }
        require(node, "bone missing");
        for (auto property :
             {node->LclTranslation, node->LclRotation, node->LclScaling})
          for (auto axis :
               {FBXSDK_CURVENODE_COMPONENT_X, FBXSDK_CURVENODE_COMPONENT_Y,
                FBXSDK_CURVENODE_COMPONENT_Z}) {
            auto curve = property.GetCurve(layer, axis);
            if (!curve)
              continue;
            require(curve->KeyGetCount() > 0, "empty FBX curve");
            require(std::abs(curve->KeyGetTime(0).GetSecondDouble()) < 1e-8 &&
                        std::abs(curve->KeyGetTime(curve->KeyGetCount() - 1)
                                         .GetSecondDouble() *
                                     1000 -
                                 duration) < 0.01,
                    "FBX curve does not span the clip");
            bool changes = false;
            for (int k = 0; k < curve->KeyGetCount(); ++k) {
              if (!std::isfinite(curve->KeyGetValue(k)))
                ++nonfinite;
              if (k && curve->KeyGetTime(k) <= curve->KeyGetTime(k - 1))
                ++errors;
              if (k && std::abs(curve->KeyGetValue(k) - curve->KeyGetValue(0)) >
                           1e-5)
                changes = true;
            }
            if (changes)
              ++moving;
          }
        for (double ms = 0;;) {
          uint32_t time = (uint32_t)(ms + 0.5);
          if (duration > 1 && time >= duration)
            time = (uint32_t)duration - 1;
          FbxTime ft;
          ft.SetSecondDouble(ms / 1000.);
          auto actualT = node->LclTranslation.EvaluateValue(ft);
          auto actualS = node->LclScaling.EvaluateValue(ft);
          FbxAMatrix rotation;
          rotation.SetR(node->LclRotation.EvaluateValue(ft));
          auto actualQ = rotation.GetQ();
          auto expectedT = glm::vec3(0), expectedS = glm::vec3(1);
          glm::fquat expectedQ(1, 0, 0, 0);
          if (!rawT.values.empty())
            expectedT = rawT.sample(time);
          if (!rawS.values.empty())
            expectedS = rawS.sample(time);
          if (!rawR.values.empty())
            expectedQ = rawR.sample(time);
          auto pivot = defs[bone].pivot;
          if (defs[bone].parent >= 0)
            pivot -= defs[defs[bone].parent]
                         .pivot; // root rest translation follows createSkeleton
          expectedT += pivot;
          double td = 0, sd = 0, dot = 0, norm = 0;
          for (int a = 0; a < 3; ++a) {
            require(std::isfinite(actualT[a]) && std::isfinite(actualS[a]) &&
                        std::isfinite(expectedT[a]) &&
                        std::isfinite(expectedS[a]),
                    "nonfinite evaluated vector");
            td = std::max(
                td, std::abs(actualT[a] - (double)(expectedT[a] * 91.44f)));
            sd = std::max(sd, std::abs(actualS[a] - expectedS[a]));
          }
          double q[] = {expectedQ.x, expectedQ.y, expectedQ.z, expectedQ.w};
          for (int a = 0; a < 4; ++a) {
            require(std::isfinite(actualQ[a]) && std::isfinite(q[a]),
                    "nonfinite evaluated rotation");
            dot += actualQ[a] * q[a];
            norm += q[a] * q[a];
          }
          require(norm > 0, "zero source quaternion");
          double rd = std::abs(1 - std::abs(dot) / std::sqrt(norm));
          maxTrans = std::max(maxTrans, td);
          maxScale = std::max(maxScale, sd);
          maxRot = std::max(maxRot, rd);
          if (!std::isfinite(td) || !std::isfinite(sd) || !std::isfinite(rd) ||
              td > 0.002 || sd > 0.0002 || rd > 0.0002)
            ++errors;
          ++values;
          if (ms == duration)
            break;
          ms = std::min(duration, ms + 1000. / 30.);
        }
      }
      require(moving > 0, "clip has no movement");
      totalErrors += errors + nonfinite;
      QJsonObject result;
      result["sequence"] = idx;
      result["take"] = stack->GetName();
      result["durationMs"] = duration;
      result["samples"] = values;
      result["movingCurves"] = moving;
      result["nonfinite"] = nonfinite;
      result["errors"] = errors;
      result["maxTranslationError"] = maxTrans;
      result["maxScaleError"] = maxScale;
      result["maxQuaternionError"] = maxRot;
      results.append(result);
      std::cout << idx << " samples=" << values << " errors=" << errors
                << " finiteFailures=" << nonfinite << " trans=" << maxTrans
                << " rot=" << maxRot << "\n";
    }
    std::ofstream out(argv[3]);
    out << QJsonDocument(results).toJson().constData();
    mgr->Destroy();
    return totalErrors ? 1 : 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 2;
  }
}
