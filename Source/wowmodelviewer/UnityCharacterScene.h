/*
 * UnityCharacterScene.h
 *
 * The RESOLVED state of a playable character, as the embedded Unity viewport needs it to draw the
 * same character the host has resolved. Nothing here decides anything about a character: every
 * value is read from what WoWModel::refresh(), CharDetails, WoWItem and refreshMerging() already
 * computed on the host's model (the archived OpenGL canvas's, which no longer draws) --
 *
 *   body         the character model's own texture slots, bound exactly as its render passes bind
 *                them (a FileDataID, or the host-composited body / eye image), its own geoset
 *                display flags, and whether each hand is closed around a weapon;
 *   merged       every model refreshMerging() laid into the character (collection armour and
 *                customization parts), in merge order, with the texture each of its slots binds
 *                inside the character, the display flags of its geoset copies, and the bone table
 *                that rebound its vertices onto the character's skeleton;
 *   attachments  every item model attached to the character, at the attachment id it was attached
 *                at (after the sheathe and shield rules), with its own textures and geosets;
 *   mount        (protocol 5, only while the character rides one) the mount model the character's node
 *                hangs from: where on it the character sits, as the mount's own attachment table places
 *                it, the rider's scale, the mount's own textures, geosets and particle colours, and what
 *                both models are playing.
 *
 * See UnityIpcServer.h (characterScene / characterImage, MOUNTED CHARACTERS) for the wire shape.
 */

#ifndef UNITYCHARACTERSCENE_H
#define UNITYCHARACTERSCENE_H

#include <functional>

#include <QImage>
#include <QJsonObject>
#include <QString>

class WoWModel;

class UnityCharacterScene
{
public:
  // Hands out the id a composited image travels under ("body", "eyes"), sending it first when the
  // player does not have those pixels yet. See UnityIpcServer::shareCharacterImage.
  using ImageRef = std::function<QString(const QString & kind, const QImage & image)>;

  // The mount a character rides, as the caller knows it: the model on the character node's parent (the
  // mount choice puts it on the canvas root, CharControl::OnUpdateItem), the character node's attachment id,
  // and the host's own identity for the mount -- CharControl's mount serial and the display it was chosen by.
  struct Mount
  {
    WoWModel * model = nullptr;
    int attachmentId = 0;
    unsigned int serial = 0;
    int displayId = 0;
  };

  // The scene for a character model (charModelDetails.isChar). Counts for the log.
  struct Summary
  {
    int bodyTextures = 0;
    int images = 0;
    int merged = 0;
    int attachments = 0;
    bool mount = false;       // the scene carries a mount
  };
  // mount: the mount the character rides, described as the scene's "mount"; null for none.
  static QJsonObject build(WoWModel * character, const ImageRef & imageRef, Summary & summary,
                           const Mount * mount = nullptr);

  // The scene's "mount" on its own: build() adds exactly this. Also what the host logs about a mount a player
  // older than protocol 5 is not sent.
  static QJsonObject buildMount(WoWModel * character, const Mount & mount);

  // A fingerprint of everything build() reads. Cheap enough to take on every canvas tick; equal
  // fingerprints mean an equal scene. mount as for build(); what the two models are PLAYING is not part of
  // it -- a changed animation is pushed, not described again.
  static quint64 signature(WoWModel * character, const Mount * mount = nullptr);
};

#endif // UNITYCHARACTERSCENE_H
