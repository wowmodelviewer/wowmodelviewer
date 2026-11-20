# The native port: macOS, Windows and Linux without Unity

This fork draws the viewport natively with Vulkan (MoltenVK on macOS) instead of embedding a Unity player, and builds
on macOS, Windows and Linux. The work is staged; each stage is its own pull request.

| Stage | What it brings | State |
|---|---|---|
| 1 | Builds on all three systems. Unity removed. A native Vulkan viewport for models, characters (composited skins, eyes, geosets, equipment, mounts), particles, ribbons and buildings (WMO groups and doodads). Client discovery for every launcher (Battle.net's list, the usual folders, Wine/Proton prefixes on Linux) and every product: Retail, Classic, Classic Era, Anniversary, WoW Forever. Packaging: an ad-hoc-signed macOS app and DMG, a Linux AppImage, the Windows build. | this PR |
| 2 | The look of the Unity viewport's lighting brought over in full (its cast shadows and contact occlusion; the preview rig is in Stage 1), the Lighting panel driving the viewport again, the FBX exporter's combiner bake on the renderer. | next |
| 3 | The world viewer (Maps), natively: map catalogue and tile grid UI back, ADT terrain and its materials, world models and doodads in the world, streaming, liquids, light, sky and fog -- the behaviour `Tools/UnityRendererProject` (C#) implements, ported to C++ on the renderer. | later |

## How a frame is drawn

```
ModelCanvas::RenderFrame                       (Source/wowmodelviewer/modelcanvas.cpp, every animation tick)
  buildFrame -> wmv::RenderList::Scope          make this frame's list current
    root->draw(), root->drawParticles()         the model code, unchanged in shape, records into the list
      WoWModel::drawModel -> ModelRenderPass::init/render     materials (blend, depth, cull, textures, UV sources,
                                                              texture matrices, M2 combiner id) + skinned vertices
      ParticleSystem::draw, RibbonEmitter::draw              camera-facing quads, strips
      Attachment / WMOModelInstance                           the modelview matrix stack (push/multiply/pop)
  wmv::vk::Viewport::render(list, settings, w, h)              (Source/render/VulkanViewport.cpp)
    upload textures the list names that the GPU does not have in their current generation
    one pipeline per (blend, depth, cull, topology, wireframe); per-draw data in a storage buffer
    model.vert / model.frag (Source/render/shaders)            GL fixed function + the 37 M2 pixel combiners
```

- **The wow library has no graphics API.** `RenderList` (Source/games/wow/RenderList.h) is the contract: what the
  model code used to say to OpenGL, as data. It keeps GL's one implicit piece of state, the modelview stack, so the
  attachment and doodad code ported line for line.
- **Textures are decoded on the CPU** (Texture.cpp; BCn by BlockDecode.cpp, the S3TC rules) into RGBA8 mip chains held
  by `TextureManager` under integer ids. Composites (body, eyes) are textures the model owns, re-uploaded when their
  generation changes. Exporters read pixels through `core::readTexturePixels`.
- **Clip space and winding**: the model code builds GL projections; the renderer maps z from [-1, 1] to [0, 1] and flips
  y with a negative viewport height, which keeps GL's counter-clockwise front faces.
- **Lighting** is the Unity viewport's preview rig in shader form: an ambient floor, a key down the world's vertical and
  a front fill from the camera, under a ceiling (`FrameSettings`). Unlit passes take their colour as is.
- **Captures** (screenshots, image sequences, the GIF/AVI exporters, the headless `-screenshot`) draw the same list into
  an offscreen image of any size. Transparent sequence frames keep the black/white two-render matte.

## Per platform

| | Surface | Packaging | User files |
|---|---|---|---|
| macOS | `CAMetalLayer` on the canvas's NSView (`PlatformSurfaceMac.mm`) | `installer/macos/make-bundle.sh`: Qt, wx, the Vulkan loader and MoltenVK inside the app (ICD at `Contents/Resources/vulkan/icd.d`), ad-hoc signature, DMG | `~/Library/Application Support/WoW Model Viewer` |
| Windows | the canvas's HWND | the existing Inno Setup installer (`installer/`) | next to the exe |
| Linux | the canvas's GdkWindow on X11 (XWayland under Wayland: `GDK_BACKEND=x11`) | `installer/linux/make-appimage.sh` (system Vulkan loader and drivers) | `~/.local/share/wowmodelviewer`, or the exe's folder with a `portable` file |

The viewer runs with that folder as its working directory (`AppPaths::prepareHome`), linking the program's read-only
data (`games/`, `dbd/`) into it, so every relative path the code uses works unchanged.

On macOS the viewer's sources compile as Objective-C++: wxWidgets and Qt both forward-declare Cocoa classes, and their
C++ declarations collide. Native buttons cannot be owner-drawn on macOS or GTK, so `UiButton` and `UiColourSwatch` give
the native button painted faces there (`UiControls.cpp`).

## Checking it

- `wowmodelviewer -mo <model path> -screenshot out.png` renders headless from the first client found;
  `WMV_RENDER_DEBUG=1` logs what each capture draws, `WMV_VULKAN_VALIDATION=1` turns the validation layer on,
  `WMV_DEBUG_NOCULL` / `WMV_DEBUG_NOALPHATEST` take culling or the alpha test out to tell them apart.
- Stage 1 was checked on macOS (Apple M5 Max, MoltenVK 1.4) against WoW Forever 1.60.1.70959: creatures, an SD and an
  HD character with randomised customization, the packaged app with its bundled MoltenVK. Windows and Linux are built
  by CI (`.github/workflows/build.yml`).

## What Stage 1 leaves out

- The Unity player's extra lighting (cast shadows, contact occlusion) -- Stage 2.
- The Maps viewer -- Stage 3. Its tests in `app.cpp` (terrain, streaming, map assets) went with the Unity IPC they
  exercised; their history is in git.
- The FBX exporter's combiner bake (`RenderTexture`, GL). The exporter is not built without Autodesk's SDK and is not
  in a release; it returns on the renderer in Stage 2.
