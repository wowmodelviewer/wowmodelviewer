// Reimport actual FBX metadata independently of the exporter and JSON writer.
#include <fbxsdk.h>
#include <iostream>
#include <iomanip>
#include <stdexcept>
int main(int argc, char ** argv) {
  try {
    if(argc!=2) throw std::runtime_error("usage: inspect-event-fbx file.fbx");
    auto * manager=FbxManager::Create();
    struct Cleanup { FbxManager * m; ~Cleanup(){m->Destroy();} } cleanup{manager};
    manager->SetIOSettings(FbxIOSettings::Create(manager,IOSROOT));
    manager->GetIOSettings()->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA,false);
    auto * scene=FbxScene::Create(manager,"");auto * reader=FbxImporter::Create(manager,"");
    if(!reader->Initialize(argv[1],-1,manager->GetIOSettings()) || !reader->Import(scene))
      throw std::runtime_error(reader->GetStatus().GetErrorString());
    auto * info=scene->GetSceneInfo();
    auto property=info?info->FindProperty("WMVAnimationEvents"):FbxProperty();
    std::cout<<"{\"sidecar\":"<<std::quoted(property.IsValid()?property.Get<FbxString>().Buffer():"")<<",\"clips\":[";
    for(int i=0;i<scene->GetSrcObjectCount<FbxAnimStack>();++i) {
      auto * take=scene->GetSrcObject<FbxAnimStack>(i);auto span=take->GetLocalTimeSpan();
      if(i)std::cout<<",";
      std::cout<<"{\"take\":"<<std::quoted(take->GetName())<<",\"durationMs\":"<<std::setprecision(16)<<span.GetDuration().GetSecondDouble()*1000.<<"}";
    }
    std::cout<<"]}\n";return 0;
  } catch(const std::exception & e) {std::cerr<<e.what();return 1;}
}
