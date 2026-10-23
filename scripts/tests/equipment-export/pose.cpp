// Independent FBX SDK skin/binding check and neutral geometry dump for visual QA.
// No consumer or WMV runtime is used to evaluate these exported vertices.
#include <fbxsdk.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include <stdexcept>

static void require(bool ok, const char * text) { if (!ok) throw std::runtime_error(text); }
static QJsonObject json(const QString & path) { QFile f(path); require(f.open(QIODevice::ReadOnly), "catalog unreadable"); return QJsonDocument::fromJson(f.readAll()).object(); }
static FbxScene * load(FbxManager * m, const QString & path) {
  auto * s = FbxScene::Create(m, ""); auto * i = FbxImporter::Create(m, "");
  require(i->Initialize(path.toUtf8().constData(), -1, m->GetIOSettings()) && i->Import(s), "FBX unreadable"); i->Destroy(); return s;
}
static FbxNode * find(FbxNode * node, const QString & name) {
  if (name == QString::fromUtf8(node->GetName())) return node;
  for (int i = 0; i < node->GetChildCount(); ++i) if (auto * found = find(node->GetChild(i), name)) return found;
  return nullptr;
}
static int boneIndex(const char * name) {
  const QString s = QString::fromUtf8(name); const int at = s.lastIndexOf("_bone_"); require(at >= 0, "unknown bone name");
  bool ok; int index = s.mid(at + 6).toInt(&ok); require(ok, "invalid bone index"); return index;
}
static FbxAMatrix rest(FbxNode * node) {
  FbxAMatrix local; local.SetTRS(node->LclTranslation.Get(), node->LclRotation.Get(), node->LclScaling.Get());
  return node->GetParent() ? rest(node->GetParent()) * local : local;
}
static double delta(const FbxAMatrix & a, const FbxAMatrix & b) {
  double d = 0; for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) d = (std::max)(d, std::abs(a.Get(r,c)-b.Get(r,c))); return d;
}
int main(int argc, char ** argv) {
  try {
    require(argc == 6, "usage: equipment-pose body-package set-package-or-- take time-ms output.json");
    const QDir bodyFolder(QString::fromUtf8(argv[1])); auto body = json(bodyFolder.filePath("catalog.json"));
    auto * manager = FbxManager::Create(); manager->SetIOSettings(FbxIOSettings::Create(manager, IOSROOT));
    manager->GetIOSettings()->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA, false);
    auto * scene = load(manager, bodyFolder.filePath(body["fbx"].toString()));
    FbxAnimStack * take = nullptr; for (int i = 0; i < scene->GetSrcObjectCount<FbxAnimStack>(); ++i) {
      auto * t = scene->GetSrcObject<FbxAnimStack>(i); if (std::string(t->GetName()) == argv[3]) take = t;
    }
    require(take != nullptr, "take absent"); scene->SetCurrentAnimationStack(take); scene->GetAnimationEvaluator()->Reset();
    FbxTime time; time.SetSecondDouble(std::atof(argv[4])/1000.);
    std::map<int,FbxNode *> joints;
    for (auto b : body["bones"].toArray()) { auto v = b.toObject(); auto * n = find(scene->GetRootNode(),v["node"].toString()); require(n, "body joint absent"); joints[v["index"].toInt()] = n; }
    QJsonArray points, triangles; int bindings = 0; double maxBindError = 0;
    auto dump = [&](FbxScene * source, const QJsonObject & catalog, bool equipment, const QJsonObject & rules) {
      std::map<QString,std::map<int,int>> materials;
      std::map<int,int> overrides; for (auto value : rules["groupOverrides"].toArray()) { auto g=value.toObject(); overrides[g["group"].toInt()]=g["variant"].toInt(); }
      std::set<int> hidden; for (auto value : rules["hideGroups"].toArray()) hidden.insert(value.toInt());
      for (auto value : catalog["geometry"].toArray()) {
        auto g=value.toObject(); int geoset=g["geoset"].toInt();
        bool visible = equipment || g["defaultVisible"].toBool();
        if (g["owner"].toString()=="base" && geoset != 0) {
          int group=geoset/100; if (overrides.count(group)) visible=geoset%100==overrides[group];
          if (hidden.count(group)) visible=false;
          // Clothing supplied by the equipment package replaces that library surface.
          if (!equipment && !rules.isEmpty() && group>=4) visible=false;
        }
        if (visible) materials[g["node"].toString()][g["materialIndex"].toInt()]=equipment ? 1 : 0;
      }
      for (const auto & entry : materials) {
        auto * node=find(source->GetRootNode(), entry.first); require(node && node->GetMesh(), "mesh absent"); auto * mesh=node->GetMesh();
        std::vector<FbxVector4> posed(mesh->GetControlPointsCount(),FbxVector4(0,0,0,0)); std::vector<double> weights(posed.size(),0);
        for (int d=0;d<mesh->GetDeformerCount(FbxDeformer::eSkin);++d) {
          auto * skin=(FbxSkin *)mesh->GetDeformer(d,FbxDeformer::eSkin);
          for (int c=0;c<skin->GetClusterCount();++c) {
            auto * cluster=skin->GetCluster(c); require(cluster->GetLink(), "unlinked cluster"); int b=boneIndex(cluster->GetLink()->GetName()); require(joints.count(b), "bone missing in body");
            FbxAMatrix link, geometry; cluster->GetTransformLinkMatrix(link); cluster->GetTransformMatrix(geometry);
            double e=delta(link,rest(joints[b])); maxBindError=(std::max)(maxBindError,e); require(e<.0001, "equipment/character bind mismatch"); ++bindings;
            auto transform=joints[b]->EvaluateGlobalTransform(time)*link.Inverse()*geometry;
            for (int k=0;k<cluster->GetControlPointIndicesCount();++k) {
              int index=cluster->GetControlPointIndices()[k]; double weight=cluster->GetControlPointWeights()[k]; require(index>=0 && index<(int)posed.size(),"bad skin index");
              auto v=transform.MultT(mesh->GetControlPointAt(index)); for(int a=0;a<3;++a) posed[index][a]+=v[a]*weight; weights[index]+=weight;
            }
          }
        }
        if (!mesh->GetDeformerCount(FbxDeformer::eSkin)) {
          FbxNode * ancestor=node->GetParent(); while(ancestor && !ancestor->GetSkeleton()) ancestor=ancestor->GetParent();
          FbxAMatrix transform=rest(node);
          if(ancestor) { int b=boneIndex(ancestor->GetName()); require(joints.count(b),"rigid bone missing"); require(delta(rest(ancestor),rest(joints[b]))<.0001,"rigid bind mismatch"); transform=joints[b]->EvaluateGlobalTransform(time)*rest(ancestor).Inverse()*rest(node); ++bindings; }
          for(int k=0;k<(int)posed.size();++k) { posed[k]=transform.MultT(mesh->GetControlPointAt(k)); weights[k]=1; }
        }
        auto * material=mesh->GetElementMaterial(); std::map<int,int> remap;
        for(int p=0;p<mesh->GetPolygonCount();++p) {
          int mat=material ? material->GetIndexArray().GetAt(p) : 0; if(!entry.second.count(mat)) continue;
          require(mesh->GetPolygonSize(p)==3,"nontriangular face"); QJsonArray face;
          for(int c=0;c<3;++c) {
            int index=mesh->GetPolygonVertex(p,c); require(std::abs(weights[index]-1)<.0001,"unnormalized weights");
            if(!remap.count(index)) { auto v=posed[index]; require(std::isfinite(v[0]+v[1]+v[2]),"nonfinite skin output"); remap[index]=points.size(); points.append(QJsonArray{v[0],v[1],v[2]}); }
            face.append(remap[index]);
          }
          face.append(equipment?1:0); triangles.append(face);
        }
      }
    };
    QJsonObject rules; QDir set(QString::fromUtf8(argv[2])); QJsonObject index;
    auto select=[&](const QJsonObject & item) {
      for(auto value:item["variants"].toArray()) { auto v=value.toObject(); if(v["race"].toInt()!=2 || v["sex"].toInt()!=0)continue;
        for(auto alt:v["bodyVariants"].toArray()) { auto b=alt.toObject(); if(b["modelFileId"]==body["modelFileId"])return b["catalog"].toString(); }
      } throw std::runtime_error("no matching posture variant");
    };
    if(std::string(argv[2])!="-") { index=json(set.filePath("catalog.json")); rules=json(set.filePath(select(index))); }
    dump(scene,body,false,rules);
    if(!index.isEmpty()) for(auto value:index["pieces"].toArray()) {
      auto piece=value.toObject(); QFileInfo path(set.filePath(piece["catalog"].toString())); auto item=json(path.filePath()); QFileInfo variant(path.dir().filePath(select(item))); auto cat=json(variant.filePath());
      require(cat["modelFileId"]==body["modelFileId"],"equipment model mismatch"); auto * s=load(manager,variant.dir().filePath(cat["fbx"].toString())); dump(s,cat,true,rules); s->Destroy();
    }
    QJsonObject result{{"modelFileId",body["modelFileId"]},{"take",argv[3]},{"timeMs",std::atof(argv[4])},{"checkedBindings",bindings},{"maxBindError",maxBindError},{"points",points},{"triangles",triangles}};
    QFile out(QString::fromUtf8(argv[5])); require(out.open(QIODevice::WriteOnly),"output unreadable");out.write(QJsonDocument(result).toJson(QJsonDocument::Compact));
    std::cout<<"PASS "<<body["modelFileId"].toInt()<<" "<<argv[3]<<" bindings="<<bindings<<" bindError="<<maxBindError<<" points="<<points.size()<<" triangles="<<triangles.size()<<"\n";
    manager->Destroy(); return 0;
  }catch(const std::exception & e){std::cerr<<e.what()<<"\n";return 1;}
}
