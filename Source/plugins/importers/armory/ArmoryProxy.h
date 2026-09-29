/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

/*
 * ArmoryProxy.h
 *
 * Everything the Armory character import does before the JSON becomes a character:
 * read region/realm/name out of a pasted link, build the proxy request, run it, and
 * classify what came back. Kept out of the plugin class so it needs nothing but Qt
 * and can be exercised on its own.
 */

#ifndef _ARMORYPROXY_H_
#define _ARMORYPROXY_H_

// Qt
#include <QJsonObject>
#include <QString>

namespace ArmoryProxy
{
  // How far a request got. Everything except Ok is shown to the user by describe().
  enum class Status
  {
    Ok = 0,
    BadLink,           // not a character link this build can read
    SearchLink,        // an Armory search page (.../armory?q=name), not a character
    UnsupportedRegion, // read the link, but the profile API does not serve that region
    NoProxy,           // no proxy URL built in, and none set in Settings > General
    NetworkError,      // the proxy could not be reached at all
    NotFound,          // 404: no such character, or the profile is hidden
    AccessDenied,      // 401/403: the proxy refused the request (access key)
    RateLimited,       // 429: too many requests
    ServerError,       // any other non-2xx
    UnexpectedPayload  // 2xx, but not the appearance document we expect
  };

  // A character as named by a link. Values stay percent-encoded exactly as they appear
  // there; buildRequestUrl() normalises them.
  struct Character
  {
    QString region; // us, eu, kr or tw, optionally with a classic-/classic1x- prefix
    QString realm;  // realm slug
    QString name;   // character name
  };

  // The outcome of one proxy request.
  struct Result
  {
    Status status = Status::Ok;
    int httpStatus = 0; // 0 when no response arrived
    QString detail;     // short technical reason, for the log and the end of the message
    QJsonObject json;   // the document returned, when status is Ok
  };

  // Read region/realm/name out of an Armory link. Handles the link shapes the site has
  // used over the years; see the implementation for the list.
  Status parseCharacterUrl(const QString & url, Character & out);

  // Fill a proxy URL template ("...?region=%s&realm=%s&character=%s") with a character.
  QString buildRequestUrl(const QString & proxyTemplate, const Character & character);

  // GET the URL and classify the answer: HTTP status, JSON validity and error payloads
  // all end up in Result::status, so an error document can never pass as a character.
  Result fetchJson(const QString & requestUrl);

  // True when a document really is a character appearance (and not, say, an error body
  // that happens to be valid JSON).
  bool looksLikeAppearance(const QJsonObject & json);

  // A message for the user explaining a failed Result, naming what was asked for.
  QString describe(const Result & result, const Character & character);

  // The proxy this build calls when Settings > General has no override.
  QString defaultProxyTemplate();

  // A copy of text with the value of any secret-looking query parameter masked. Use it on
  // anything that carries a request URL into a log or a message: the proxy's access key is a
  // shared credential, and a log gets pasted into bug reports.
  QString redactSecrets(const QString & text);
}

#endif /* _ARMORYPROXY_H_ */
