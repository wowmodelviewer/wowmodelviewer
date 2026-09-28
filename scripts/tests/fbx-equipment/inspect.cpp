#include <fbxsdk.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

static void collect(FbxNode* n, std::vector<FbxNode*>& meshes)
{
  if (n->GetMesh()) meshes.push_back(n);
  for (int i = 0; i < n->GetChildCount(); ++i) collect(n->GetChild(i), meshes);
}

// Read the generated FBX independently of WMV. Report topology, materials, bone parenting,
// and actual animated world-space movement of each rigid attachment across every take.
int main(int argc, char** argv)
{
  if (argc != 2) return 2;
  FbxManager* manager = FbxManager::Create();
  manager->SetIOSettings(FbxIOSettings::Create(manager, IOSROOT));
  FbxImporter* importer = FbxImporter::Create(manager, "");
  FbxScene* scene = FbxScene::Create(manager, "");
  if (!importer->Initialize(argv[1], -1, manager->GetIOSettings()) || !importer->Import(scene))
  {
    std::cerr << importer->GetStatus().GetErrorString();
    manager->Destroy();
    return 1;
  }
  importer->Destroy();
  const int clips = scene->GetSrcObjectCount<FbxAnimStack>();
  std::cout << "{\"clips\":[";
  for (int i = 0; i < clips; ++i)
  {
    if (i) std::cout << ',';
    std::cout << std::quoted(scene->GetSrcObject<FbxAnimStack>(i)->GetName());
  }
  std::vector<FbxNode*> meshes;
  collect(scene->GetRootNode(), meshes);
  std::cout << "],\"meshes\":[";
  bool first = true;
  for (FbxNode* node : meshes)
  {
    if (!first) std::cout << ',';
    first = false;
    FbxMesh* mesh = node->GetMesh();
    int triangles = 0;
    for (int p = 0; p < mesh->GetPolygonCount(); ++p) triangles += mesh->GetPolygonSize(p) - 2;
    std::cout << "{\"name\":" << std::quoted(node->GetName())
      << ",\"triangles\":" << triangles << ",\"parent\":" << std::quoted(node->GetParent()->GetName())
      << ",\"boneParent\":" << (node->GetParent()->GetSkeleton() ? "true" : "false")
      << ",\"materials\":" << node->GetMaterialCount() << ",\"textures\":[";
    bool firstTexture = true;
    for (int i = 0; i < node->GetMaterialCount(); ++i)
      for (FbxProperty p = node->GetMaterial(i)->GetFirstProperty(); p.IsValid();
           p = node->GetMaterial(i)->GetNextProperty(p))
        for (int t = 0; t < p.GetSrcObjectCount<FbxFileTexture>(); ++t)
        {
          if (!firstTexture) std::cout << ',';
          firstTexture = false;
          std::cout << std::quoted(p.GetSrcObject<FbxFileTexture>(t)->GetFileName());
        }
    std::cout << "],\"motion\":[";
    if (node->GetParent()->GetSkeleton() && mesh->GetControlPointsCount())
      for (int i = 0; i < clips; ++i)
      {
        if (i) std::cout << ',';
        FbxAnimStack* stack = scene->GetSrcObject<FbxAnimStack>(i);
        scene->SetCurrentAnimationStack(stack);
        const FbxTimeSpan span = stack->GetLocalTimeSpan();
        FbxTime start = span.GetStart();
        const FbxDouble3 initialT = node->LclTranslation.EvaluateValue(start);
        const FbxDouble3 initialR = node->LclRotation.EvaluateValue(start);
        const FbxDouble3 initialS = node->LclScaling.EvaluateValue(start);
        const FbxVector4 initial = node->EvaluateGlobalTransform(start).MultT(mesh->GetControlPointAt(0));
        double movement = 0, localError = 0;
        for (int sample = 0; sample <= 8; ++sample)
        {
          FbxTime time;
          time.SetSecondDouble(start.GetSecondDouble() + span.GetDuration().GetSecondDouble() * sample / 8.0);
          const FbxVector4 point = node->EvaluateGlobalTransform(time).MultT(mesh->GetControlPointAt(0));
          movement = std::max(movement, (point - initial).Length());
          // Read the authored local properties directly. EvaluateLocalTransform reconstructs
          // local from inverse(parentGlobal)*global; zero-scale parent frames make that inverse
          // singular even when this rigid prop has no local animation at all.
          const FbxDouble3 t = node->LclTranslation.EvaluateValue(time);
          const FbxDouble3 r = node->LclRotation.EvaluateValue(time);
          const FbxDouble3 s = node->LclScaling.EvaluateValue(time);
          for (int c = 0; c < 3; ++c)
          {
            localError = std::max(localError, std::abs(t[c] - initialT[c]));
            localError = std::max(localError, std::abs(r[c] - initialR[c]));
            localError = std::max(localError, std::abs(s[c] - initialS[c]));
          }
        }
        std::cout << "{\"clip\":" << std::quoted(stack->GetName()) << ",\"movement\":" << movement
                  << ",\"localError\":" << localError << '}';
      }
    std::cout << "]}";
  }
  std::cout << "]}\n";
  manager->Destroy();
}
