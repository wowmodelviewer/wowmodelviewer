/*
 * UnityCharacterScene.h
 *
 * The RESOLVED state of a playable character, as the embedded Unity viewport needs it to draw the
 * same character the OpenGL canvas draws. Nothing here decides anything about a character: every
 * value is read from what WoWModel::refresh(), CharDetails, WoWItem and refreshMerging() already
 * computed for the OpenGL draw --
 *
 *   body         the character model's own texture slots, bound exactly as its render passes bind
 *                them (a FileDataID, or the host-composited body / eye image), its own geoset
 *                display flags, and whether each hand is closed around a weapon;
 *   merged       every model refreshMerging() laid into the character (collection armour and
 *                customization parts), in merge order, with the texture each of its slots binds
 *                inside the character, the display flags of its geoset copies, and the bone table
 *                that rebound its vertices onto the character's skeleton;
 *   attachments  every item model attached to the character, at the attachment id it was attached
 *                at (after the sheathe and shield rules), with its own textures and geosets.
 *
 * See UnityIpcServer.h (characterScene / characterImage) for the wire shape.
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

  // The scene for a character model (charModelDetails.isChar). Counts for the log.
  struct Summary
  {
    int bodyTextures = 0;
    int images = 0;
    int merged = 0;
    int attachments = 0;
  };
  static QJsonObject build(WoWModel * character, const ImageRef & imageRef, Summary & summary);

  // A fingerprint of everything build() reads. Cheap enough to take on every canvas tick; equal
  // fingerprints mean an equal scene.
  static quint64 signature(WoWModel * character);
};

#endif // UNITYCHARACTERSCENE_H
