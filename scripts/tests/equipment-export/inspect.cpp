#include <fbxsdk.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif

static void inspectNode(FbxNode * node, bool & first)
{
  if (!first) std::cout << ',';
  first = false;
  auto * mesh = node->GetMesh();
  std::vector<bool> used(mesh ? mesh->GetControlPointsCount() : 0, false);
  int invalidIndices = 0;
  if (mesh)
    for (int p = 0; p < mesh->GetPolygonCount(); ++p)
      for (int i = 0; i < mesh->GetPolygonSize(p); ++i)
      {
        const int index = mesh->GetPolygonVertex(p, i);
        if (index < 0 || index >= (int)used.size()) ++invalidIndices;
        else used[index] = true;
      }
  int unused = 0;
  for (bool value : used) if (!value) ++unused;
  std::cout << "{\"name\":" << std::quoted(node->GetName())
            << ",\"parent\":" << std::quoted(node->GetParent() ? node->GetParent()->GetName() : "")
            << ",\"skeleton\":" << (node->GetSkeleton() ? "true" : "false")
            << ",\"vertices\":" << used.size() << ",\"unusedVertices\":" << unused
            << ",\"invalidIndices\":" << invalidIndices
            << ",\"polygons\":" << (mesh ? mesh->GetPolygonCount() : 0) << ",\"materials\":[";
  for (int i = 0; i < node->GetMaterialCount(); ++i)
  { if (i) std::cout << ','; std::cout << std::quoted(node->GetMaterial(i)->GetName()); }
  std::vector<double> weights(used.size(), 0);
  bool skinned = false, firstCluster = true;
  std::cout << "],\"clusters\":[";
  if (mesh)
    for (int s = 0; s < mesh->GetDeformerCount(FbxDeformer::eSkin); ++s)
    {
      skinned = true;
      auto * skin = static_cast<FbxSkin *>(mesh->GetDeformer(s, FbxDeformer::eSkin));
      for (int c = 0; c < skin->GetClusterCount(); ++c)
      {
        auto * cluster = skin->GetCluster(c);
        if (!firstCluster) std::cout << ',';
        firstCluster = false;
        std::cout << std::quoted(cluster->GetLink() ? cluster->GetLink()->GetName() : "MISSING");
        for (int i = 0; i < cluster->GetControlPointIndicesCount(); ++i)
        {
          const int index = cluster->GetControlPointIndices()[i];
          if (index < 0 || index >= (int)weights.size()) ++invalidIndices;
          else weights[index] += cluster->GetControlPointWeights()[i];
        }
      }
    }
  int invalidWeights = 0;
  if (skinned) for (double weight : weights) if (!std::isfinite(weight) || std::abs(weight - 1) > 0.0001) ++invalidWeights;
  const auto translation = node->LclTranslation.Get();
  std::cout << "],\"invalidWeights\":" << invalidWeights << ",\"invalidSkinIndices\":" << invalidIndices
            << ",\"translation\":[" << translation[0] << ',' << translation[1] << ',' << translation[2] << "]}";
  for (int i = 0; i < node->GetChildCount(); ++i) inspectNode(node->GetChild(i), first);
}

static int inspectFile(const char * path)
{
  auto * manager = FbxManager::Create();
  manager->SetIOSettings(FbxIOSettings::Create(manager, IOSROOT));
  manager->GetIOSettings()->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA, false);
  auto * importer = FbxImporter::Create(manager, "");
  auto * scene = FbxScene::Create(manager, "");
  if (!importer->Initialize(path, -1, manager->GetIOSettings()) || !importer->Import(scene))
  { std::cerr << importer->GetStatus().GetErrorString(); manager->Destroy(); return 1; }
  std::cout << "{\"nodes\":[";
  bool first = true;
  inspectNode(scene->GetRootNode(), first);
  std::cout << "],\"textures\":[";
  for (int i = 0; i < scene->GetSrcObjectCount<FbxFileTexture>(); ++i)
  {
    if (i) std::cout << ',';
    auto * texture = scene->GetSrcObject<FbxFileTexture>(i);
    std::cout << "{\"relative\":" << std::quoted(texture->GetRelativeFileName()) << "}";
  }
  std::cout << "],\"clips\":" << scene->GetSrcObjectCount<FbxAnimStack>() << "}";
  manager->Destroy();
  return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t ** argv)
{
  if (argc != 2) return 2;
  const int length = WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, nullptr, 0, nullptr, nullptr);
  std::vector<char> path(length);
  if (!length || !WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, path.data(), length, nullptr, nullptr)) return 2;
  return inspectFile(path.data());
}
#else
int main(int argc, char ** argv) { return argc == 2 ? inspectFile(argv[1]) : 2; }
#endif
