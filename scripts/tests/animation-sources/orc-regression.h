// Independent source-file oracle for the SKPD posture regression. No production
// skeleton resolver, Animated<> sampler or FBX exporter is used for expectations.
#include <QFile>

struct OrcSource {
  std::vector<unsigned char> sks, skb, afid;
  std::vector<ModelAnimation> sequences;
  std::vector<ModelBoneDef> bones;
  std::vector<uint32_t> globals;
  std::map<std::pair<int, int>, int> identities;
  OrcSource(const std::string & raw, int id) {
    auto bytes = read(raw + "/" + std::to_string(id) + ".bin");
    sks = chunk(bytes, "SKS1"); skb = chunk(bytes, "SKB1"); afid = chunk(bytes, "AFID");
    auto h = rawValue<SKS1>(sks, 0);
    for (size_t i = 0; i < h.nAnimations; ++i) {
      auto s = rawValue<ModelAnimation>(sks, h.ofsAnimations + i * sizeof(ModelAnimation));
      sequences.push_back(s); identities[{s.animID, s.subAnimID}] = (int)i;
    }
    for (size_t i = 0; i < h.nGlobalSequences; ++i) globals.push_back(rawValue<uint32_t>(sks, h.ofsGlobalSequences + 4 * i));
    auto b = rawValue<SKB1>(skb, 0);
    for (size_t i = 0; i < b.nBones; ++i) bones.push_back(rawValue<ModelBoneDef>(skb, b.ofsBones + i * sizeof(ModelBoneDef)));
  }
  int externalId(int index) const {
    auto s = sequences.at(index);
    for (size_t a = 0; a < afid.size(); a += sizeof(AFID)) {
      auto f = rawValue<AFID>(afid, a);
      if (f.animId == s.animID && f.subAnimId == s.subAnimID && f.fileId) return f.fileId;
    }
    return 0;
  }
  int resolve(int index) const {
    std::set<int> seen;
    while (index >= 0 && index < (int)sequences.size() && seen.insert(index).second) {
      auto s = sequences[index];
      if ((s.flags & 0x20) || externalId(index)) return index;
      if (!(s.flags & 0x40)) return -1;
      index = s.Index;
    }
    return -1;
  }
};

static int verifyOrc(const char * rawFolder, const char * package, const char * report) {
  const std::string raw = rawFolder;
  OrcSource parent(raw, 1964411), child(raw, 1969462);
  QFile file(QString::fromUtf8(package) + "/catalog.json"); require(file.open(QIODevice::ReadOnly), "open catalog");
  auto catalog = QJsonDocument::fromJson(file.readAll()).object();
  const bool upright = catalog["modelFileId"].toInt() == 1968587;
  require(upright || catalog["modelFileId"].toInt() == 917116, "unexpected model");
  auto mgr = FbxManager::Create(); mgr->SetIOSettings(FbxIOSettings::Create(mgr, IOSROOT));
  mgr->GetIOSettings()->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA, false);
  auto importer = FbxImporter::Create(mgr, ""); auto scene = FbxScene::Create(mgr, "");
  auto fbx = QString::fromUtf8(package) + "/" + catalog["fbx"].toString();
  require(importer->Initialize(fbx.toUtf8().constData(), -1, mgr->GetIOSettings()) && importer->Import(scene), "import posture FBX");
  std::vector<FbxNode *> nodes; collect(scene->GetRootNode(), nodes);
  auto identities = catalog["animations"].toArray();
  // The supplied pre-fix packages used the documented 0..225 selection and did
  // not yet record identities. Read their names without modifying the package.
  if (identities.empty()) {
    int take = 0;
    for (const auto & sequence : parent.sequences)
      if (sequence.animID >= 0 && sequence.animID <= 225) {
        require(take < scene->GetSrcObjectCount<FbxAnimStack>(), "missing baseline take");
        identities.append(QJsonObject{{"take", scene->GetSrcObject<FbxAnimStack>(take++)->GetName()},
          {"animationId", sequence.animID}, {"subAnimationId", sequence.subAnimID}});
      }
  }
  require(identities.size() == scene->GetSrcObjectCount<FbxAnimStack>() && !identities.empty(), "animation identities absent");
  const auto & bind = upright ? child : parent;
  require(nodes.size() == bind.bones.size(), "wrong posture skeleton");
  std::vector<FbxNode *> joints(bind.bones.size());
  for (size_t b = 0; b < joints.size(); ++b) {
    const std::string suffix = "_bone_" + std::to_string(b);
    for (auto n : nodes) { std::string name = n->GetName(); if (name.size() >= suffix.size() && name.substr(name.size() - suffix.size()) == suffix) joints[b] = n; }
    require(joints[b] != nullptr, "posture joint absent");
  }
  QJsonArray results; int failures = 0, total = 0, own = 0, inherited = 0;
  for (int a = 0; a < identities.size(); ++a) {
    auto identity = identities[a].toObject();
    auto key = std::make_pair(identity["animationId"].toInt(), identity["subAnimationId"].toInt());
    const bool childClip = upright && child.identities.count(key);
    const auto & source = childClip ? child : parent;
    childClip ? ++own : ++inherited;
    require(source.identities.count(key) != 0, "unknown sequence identity");
    const int index = source.identities.at(key), resolved = source.resolve(index);
    const auto sequence = source.sequences[index];
    auto stack = scene->GetSrcObject<FbxAnimStack>(a); scene->SetCurrentAnimationStack(stack); scene->GetAnimationEvaluator()->Reset();
    require(identity["take"].toString() == QString::fromUtf8(stack->GetName()), "wrong take identity");
    const double duration = sequence.length;
    const int durationErrors = std::abs(stack->GetLocalTimeSpan().GetDuration().GetSecondDouble() * 1000 - duration) < .01 ? 0 : 1;
    std::vector<unsigned char> keys;
    if (resolved >= 0) {
      const int external = source.externalId(resolved);
      keys = sequence.flags & 0x20 ? source.skb : external ? chunk(read(raw + "/" + std::to_string(external) + ".bin"), "AFSB") : source.skb;
      if (source.sequences[resolved].flags & 0x20) keys = source.skb;
    }
    int samples = 0, errors = durationErrors; double maxT = 0, maxR = 0, maxS = 0;
    for (size_t b = 0; b < joints.size(); ++b) {
      const auto & def = source.bones[b];
      auto vector = [](glm::vec3 v) { return v; };
      auto quaternion = [](PACK_QUATERNION v) {
        auto c = [](int16_t s) { return float(s < 0 ? s + 32768 : s - 32767) / 32767.f; };
        return glm::fquat(c(v.w), c(v.x), c(v.y), c(v.z));
      };
      // Globals are inline even if the selected per-sequence source is unavailable.
      auto t = rawTrack<glm::vec3, glm::vec3>(def.translation, resolved, source.skb, keys, source.globals, sequence.length, vector);
      auto r = rawTrack<glm::fquat, PACK_QUATERNION>(def.rotation, resolved, source.skb, keys, source.globals, sequence.length, quaternion);
      auto s = rawTrack<glm::vec3, glm::vec3>(def.scaling, resolved, source.skb, keys, source.globals, sequence.length, vector);
      for (double ms = 0;; ms = std::min(duration, ms + 1000. / 30.)) {
        uint32_t time = (uint32_t)(ms + .5); if (duration > 1 && time >= duration) time = (uint32_t)duration - 1;
        FbxTime ft; ft.SetSecondDouble(ms / 1000.);
        auto at = joints[b]->LclTranslation.EvaluateValue(ft), as = joints[b]->LclScaling.EvaluateValue(ft);
        FbxAMatrix rm; rm.SetR(joints[b]->LclRotation.EvaluateValue(ft)); auto aq = rm.GetQ();
        glm::vec3 et = t.values.empty() ? glm::vec3(0) : t.sample(time), es = s.values.empty() ? glm::vec3(1) : s.sample(time);
        auto eq = r.values.empty() ? glm::fquat(1, 0, 0, 0) : r.sample(time);
        auto pivot = bind.bones[b].pivot; if (bind.bones[b].parent >= 0) pivot -= bind.bones[bind.bones[b].parent].pivot;
        et += pivot; double td = 0, sd = 0, dot = 0, norm = 0;
        for (int axis = 0; axis < 3; ++axis) { td = std::max(td, std::abs(at[axis] - double(et[axis] * 91.44f))); sd = std::max(sd, std::abs(as[axis] - es[axis])); }
        double q[] = {eq.x, eq.y, eq.z, eq.w};
        for (int axis = 0; axis < 4; ++axis) { dot += aq[axis] * q[axis]; norm += q[axis] * q[axis]; }
        require(norm > 0 && std::isfinite(norm), "invalid source quaternion");
        double rd = std::abs(1 - std::abs(dot) / std::sqrt(norm));
        maxT = std::max(maxT, td); maxS = std::max(maxS, sd); maxR = std::max(maxR, rd);
        if (!std::isfinite(td + sd + rd) || td > .002 || sd > .0002 || rd > .0002) ++errors;
        ++samples; if (ms == duration) break;
      }
    }
    failures += errors; total += samples;
    results.append(QJsonObject{{"take", identity["take"]}, {"animationId", key.first}, {"subAnimationId", key.second},
      {"sourceSkeleton", childClip ? 1969462 : 1964411}, {"sourceIndex", index}, {"resolvedIndex", resolved}, {"durationMs", duration},
      {"samples", samples}, {"errors", errors}, {"maxTranslationErrorCm", maxT}, {"maxScaleError", maxS}, {"maxQuaternionError", maxR}});
    std::cout << stack->GetName() << " samples=" << samples << " errors=" << errors << " T=" << maxT << " R=" << maxR << "\n";
  }
  QJsonObject output{{"modelFileId", upright ? 1968587 : 917116}, {"build", "12.1.5.70391"}, {"product", "wow"},
    {"ownClips", own}, {"parentClips", inherited}, {"samples", total}, {"errors", failures}, {"clips", results}};
  std::ofstream out(report); out << QJsonDocument(output).toJson().constData(); mgr->Destroy();
  return failures ? 1 : 0;
}
