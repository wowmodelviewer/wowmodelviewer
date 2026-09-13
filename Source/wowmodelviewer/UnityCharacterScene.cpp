/*
 * UnityCharacterScene.cpp
 */

#include "UnityCharacterScene.h"

#include <algorithm>
#include <set>
#include <vector>

#include <QJsonArray>

#include "Attachment.h"
#include "ModelRenderPass.h"
#include "wow_enums.h"
#include "WoWItem.h"
#include "WoWModel.h"

namespace
{
  // An item model attached to the character, and the attachment id it hangs at.
  struct AttachedModel
  {
    WoWModel * model;
    int attachmentId;
  };

  // The item models attached to the character: the equipment's models (WoWItem::models()), each at
  // the id of the Attachment node that carries it.
  //
  // The node's id, not the item's slot key: a sheathed weapon and a shield hang at a different id
  // from the one their slot names (WoWItem::refresh). And the node a model points back at
  // (Displayable::attachment, set by the node that took it last -- Attachment::setModel), not a
  // search of the tree: Attachment::delSlot skips the entry after every one it erases, so the tree
  // keeps stale nodes for swapped items, some holding models that no longer exist. A model whose
  // back-pointer is not a child of the character's own node is not attached to it.
  std::vector<AttachedModel> attachedModels(WoWModel * character)
  {
    std::vector<AttachedModel> out;
    if (!character || !character->attachment)
      return out;
    std::set<WoWModel *> seen;
    for (WoWItem * item : *character)
    {
      if (!item)
        continue;
      const std::map<POSITION_SLOTS, WoWModel *> models = item->models();
      for (const auto & kv : models)
      {
        WoWModel * m = kv.second;
        if (!m || !m->gamefile || seen.count(m))
          continue;
        const Attachment * node = m->attachment;
        if (!node || node->parent != character->attachment)
          continue;
        out.push_back({ m, node->id });
        seen.insert(m);
      }
    }
    // Item iteration order is unspecified (Container keeps an unordered_set); the scene is not.
    std::sort(out.begin(), out.end(), [](const AttachedModel & a, const AttachedModel & b) {
      if (a.attachmentId != b.attachmentId)
        return a.attachmentId < b.attachmentId;
      return a.model->gamefile->fileDataId() < b.model->gamefile->fileDataId();
    });
    return out;
  }

  int ownTextureCount(const WoWModel * m)
  {
    return (int)std::min<uint32>(m->header.nTextures, (uint32)TEXTURE_MAX);
  }

  // A key for a part that survives from one scene to the next exactly as long as the host's model
  // object does: two merged or attached models can share a FileDataID, never an address.
  QString partKey(char kind, const WoWModel * m)
  {
    return QString("%1%2").arg(kind).arg((qulonglong)(quintptr)m, 0, 16);
  }

  // One texture binding, from the GL texture a render pass binds -- WoWModel::getGLTexture, the call
  // ModelRenderPass::init makes -- so the answer is the OpenGL viewport's by construction.
  //   name 0                     the body composite (CharTexture::compose uploads into name 0)
  //   the eye composite's name   the eye composite
  //   INVALID_TEX                nothing bound: false
  //   anything else              the file the texture manager created that name from
  bool binding(QJsonObject & t, GLuint id, WoWModel * character, const UnityCharacterScene::ImageRef & imageRef,
               UnityCharacterScene::Summary & summary)
  {
    if (id == ModelRenderPass::INVALID_TEX)
      return false;
    if (id == 0)
    {
      const QImage & body = character->tex.lastImage();
      if (body.isNull())
        return false;
      t["image"] = imageRef(QStringLiteral("body"), body);
      summary.images++;
      return true;
    }
    if (id == character->eyeCompositeTexture() && !character->eyeCompositeImage().isNull())
    {
      t["image"] = imageRef(QStringLiteral("eyes"), character->eyeCompositeImage());
      summary.images++;
      return true;
    }
    const int fdid = WoWModel::fileDataIdForGLTexture(id);
    if (fdid <= 0)
      return false;
    t["fileDataID"] = fdid;
    return true;
  }

  void addTexture(QJsonArray & out, int slot, int type, GLuint id, WoWModel * character,
                  const UnityCharacterScene::ImageRef & imageRef, UnityCharacterScene::Summary & summary)
  {
    QJsonObject t;
    t["slot"] = slot;
    t["type"] = type;
    if (binding(t, id, character, imageRef, summary))
      out.append(t);
  }

  QJsonArray bits(const std::vector<ModelGeosetHD *> & geosets, size_t start, size_t count)
  {
    QJsonArray arr;
    for (size_t i = start; i < start + count && i < geosets.size(); i++)
      arr.append(geosets[i] && geosets[i]->display ? 1 : 0);
    return arr;
  }

  // FNV-1a, 64-bit.
  struct Fnv
  {
    quint64 h = 1469598103934665603ULL;
    void add(quint64 v)
    {
      for (int i = 0; i < 8; i++)
      {
        h ^= (v >> (i * 8)) & 0xff;
        h *= 1099511628211ULL;
      }
    }
  };
}

QJsonObject UnityCharacterScene::build(WoWModel * character, const ImageRef & imageRef, Summary & summary)
{
  QJsonObject scene;
  if (!character || !character->gamefile)
    return scene;

  // ---- the character's own model ----------------------------------------------------------
  {
    QJsonObject body;
    QJsonArray textures;
    for (int slot = 0; slot < ownTextureCount(character); slot++)
      addTexture(textures, slot, character->textureTypeForSlot(slot), character->getGLTexture((uint16)slot),
                 character, imageRef, summary);
    summary.bodyTextures = textures.size();
    body["textures"] = textures;
    const size_t own = std::min(character->ownGeosetCount(), character->geosets.size());
    body["submeshCount"] = (int)own;
    body["submeshVisible"] = bits(character->geosets, 0, own);

    // THE CLOSED HAND (WoWModel::calcBones). While a weapon is held, the five finger key bones of that
    // hand are posed from the HandsClosed sequence at time 1 instead of the playing one. The sequence
    // and the bones are resolved here, from the lookups the host reads them through, so the player
    // applies the same override to the same bones.
    body["closeRightHand"] = character->charModelDetails.closeRHand;
    body["closeLeftHand"] = character->charModelDetails.closeLHand;
    {
      int fist = 0;
      if (character->animLookups.size() > (size_t)ANIMATION_HANDSCLOSED &&
          character->animLookups[ANIMATION_HANDSCLOSED] > 0)
        fist = character->animLookups[ANIMATION_HANDSCLOSED];
      QJsonArray right, left;
      for (int i = 0; i < 5; i++)
      {
        if (character->keyBoneLookup[BONE_RFINGER1 + i] > -1)
          right.append((int)character->keyBoneLookup[BONE_RFINGER1 + i]);
        if (character->keyBoneLookup[BONE_LFINGER1 + i] > -1)
          left.append((int)character->keyBoneLookup[BONE_LFINGER1 + i]);
      }
      body["fistSequence"] = fist;
      body["fistTimeMs"] = 1;
      body["rightFingerBones"] = right;
      body["leftFingerBones"] = left;
    }
    scene["body"] = body;
  }

  // ---- merged models, in merge order ---------------------------------------------------------
  {
    QJsonArray merged;
    for (const WoWModel::MergedPart & part : character->mergedParts())
    {
      if (!part.model || !part.model->gamefile)
        continue;
      QJsonObject o;
      const int fdid = (int)part.model->gamefile->fileDataId();
      o["key"] = partKey('m', part.model);
      o["fileDataID"] = fdid;
      o["mergeIndex"] = (int)part.mergeIndex;
      // Its slots as the CHARACTER binds them: refreshMerging appended this model's texture tables at
      // mergeIndex * TEXTURE_MAX, and redirected its skin-extra slot to the body composite when it has
      // none of its own. Reading the character's combined table at that stride gives exactly what the
      // merged render passes sample.
      QJsonArray textures;
      for (int slot = 0; slot < ownTextureCount(part.model); slot++)
      {
        const size_t combined = (size_t)part.mergeIndex * TEXTURE_MAX + (size_t)slot;
        if (combined > 0xFFFF)
          break;
        addTexture(textures, slot, part.model->textureTypeForSlot(slot), character->getGLTexture((uint16)combined),
                   character, imageRef, summary);
      }
      o["textures"] = textures;
      // The passes refreshMerging pointed at the character's hand texture instead of their own.
      if (!part.handSubmeshes.empty())
      {
        QJsonArray hands;
        for (int s : part.handSubmeshes)
          hands.append(s);
        o["handSubmeshes"] = hands;
        QJsonObject hand;
        hand["slot"] = -1;
        if (binding(hand, character->getGLTexture(part.handTexIndex), character, imageRef, summary))
          o["handTexture"] = hand;
      }
      o["submeshCount"] = (int)part.geosetCount;
      o["submeshVisible"] = bits(character->geosets, part.geosetStart, part.geosetCount);
      QJsonArray boneMap;
      for (int16 b : part.boneMap)
        boneMap.append((int)b);
      o["boneMap"] = boneMap;
      merged.append(o);
    }
    summary.merged = merged.size();
    scene["merged"] = merged;
  }

  // ---- attached item models -------------------------------------------------------------------
  {
    QJsonArray attachments;
    for (const AttachedModel & a : attachedModels(character))
    {
      WoWModel * m = a.model;
      QJsonObject o;
      const int fdid = (int)m->gamefile->fileDataId();
      o["key"] = partKey('a', m);
      o["fileDataID"] = fdid;
      o["attachmentId"] = a.attachmentId;
      // WHERE, as the host resolves it: WoWModel::setupAtt looks the id up in the CHARACTER's
      // attachment table and ModelAttachment::setup multiplies that bone's matrix by a translation to
      // the attachment's position. An id the character has no entry for gets no transform at all.
      int bone = -1;
      glm::vec3 pos(0.0f);
      if (a.attachmentId >= 0 && a.attachmentId < (int)WoWModel::ATT_MAX)
      {
        const int l = character->attLookup[a.attachmentId];
        if (l > -1 && l < (int)character->atts.size())
        {
          bone = character->atts[l].bone;
          pos = character->atts[l].pos;
        }
      }
      o["bone"] = bone;
      o["position"] = QJsonArray{ pos.x, pos.y, pos.z };
      o["mirrored"] = m->mirrored_;
      o["visible"] = m->showModel;
      o["scale"] = m->scale_;
      QJsonArray textures;
      for (int slot = 0; slot < ownTextureCount(m); slot++)
        addTexture(textures, slot, m->textureTypeForSlot(slot), m->getGLTexture((uint16)slot),
                   character, imageRef, summary);
      o["textures"] = textures;
      const size_t own = std::min(m->ownGeosetCount(), m->geosets.size());
      o["submeshCount"] = (int)own;
      o["submeshVisible"] = bits(m->geosets, 0, own);
      attachments.append(o);
    }
    summary.attachments = attachments.size();
    scene["attachments"] = attachments;
  }

  return scene;
}

quint64 UnityCharacterScene::signature(WoWModel * character)
{
  Fnv f;
  if (!character)
    return f.h;
  f.add((quint64)(quintptr)character);
  f.add(character->stateVersion());
  f.add(character->charModelDetails.closeRHand ? 1 : 0);
  f.add(character->charModelDetails.closeLHand ? 2 : 0);
  // The flags a Geosets checkbox changes without a refresh, body and merged copies alike.
  f.add(character->geosets.size());
  for (size_t i = 0; i < character->geosets.size(); i++)
    f.add(character->geosets[i] && character->geosets[i]->display ? 1 : 0);
  for (int slot = 0; slot < ownTextureCount(character); slot++)
    f.add(character->getGLTexture((uint16)slot));
  // Attached items change without the character refreshing: Model Control's render and scale, a
  // selection that re-skins an item model, its own geoset checkboxes.
  for (const AttachedModel & a : attachedModels(character))
  {
    f.add((quint64)(quintptr)a.model);
    f.add((quint64)a.attachmentId);
    f.add(a.model->showModel ? 1 : 0);
    f.add(a.model->mirrored_ ? 1 : 0);
    f.add((quint64)(a.model->scale_ * 1000.0f));
    for (int slot = 0; slot < ownTextureCount(a.model); slot++)
      f.add(a.model->getGLTexture((uint16)slot));
    for (size_t i = 0; i < a.model->geosets.size(); i++)
      f.add(a.model->geosets[i] && a.model->geosets[i]->display ? 1 : 0);
  }
  return f.h;
}
