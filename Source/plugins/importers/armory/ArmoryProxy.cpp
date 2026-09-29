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
 * ArmoryProxy.cpp
 */

#include "ArmoryProxy.h"

// Qt
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

// Other libraries
#include "logger/Logger.h"

namespace
{
  // Built-in armory proxy URL baked into the build. The client holds NO Blizzard API
  // credentials; it calls a small self-hosted proxy, which performs the OAuth
  // client_credentials grant + appearance fetch server-side and returns the JSON.
  // printf-style template: 1st %s = region, 2nd = realm slug, 3rd = character name
  // (lower-cased). Points at this project's own self-hosted proxy; override at runtime
  // in Settings > General. The &key= is a public rate-limit gate (it ships in the binary
  // by design) -- the Blizzard client secret lives only on the proxy server, never here.
  // See armory-proxy/README.md.
  const QString DEFAULT_PROXY_URL =
    "https://wmv-armory.wmwarmory.workers.dev/?region=%s&realm=%s&character=%s&key=X5qtjCWegxwQfk40PS8EGcnNdD3Y";

  // Longest error body worth keeping in a log line / message tail.
  const int MAX_DETAIL_CHARS = 300;

  // How long to wait for the proxy before giving up. The proxy's own upstream timeout is
  // 12 s, so this only trips when the proxy itself stops answering.
  const int REQUEST_TIMEOUT_MS = 20000;

  // The regions the profile API serves. A "classic-"/"classic1x-" prefix is passed on to
  // the proxy, which maps it to the matching classic namespace.
  bool isRegionToken(const QString & segment)
  {
    QString s = segment.toLower();
    if (s.startsWith("classic1x-"))
      s = s.mid(10);
    else if (s.startsWith("classic-"))
      s = s.mid(8);

    return (s == "us" || s == "eu" || s == "kr" || s == "tw");
  }

  // Oldest links name no region at all, only the site locale (en-gb, pt-br, ...), so the
  // region has to come from the locale's country. The Latin-American locales are served by
  // the US region; the rest follow their country code.
  QString regionFromLocale(const QString & locale)
  {
    const QStringList parts = locale.toLower().split('-', QString::SkipEmptyParts);
    if (parts.size() != 2)
      return QString();

    const QString & country = parts.at(1);
    if (country == "us" || country == "mx" || country == "br")
      return "us";
    if (country == "gb" || country == "fr" || country == "de" || country == "es" ||
        country == "it" || country == "ru" || country == "pt")
      return "eu";
    if (country == "kr")
      return "kr";
    if (country == "tw")
      return "tw";

    // Anything else (e.g. zh-cn) is a region the profile API does not serve. Returning it
    // unchanged lets the caller say "unsupported region" instead of "unreadable link".
    return country;
  }

  // Percent-decode once, lower-case, then encode once, so a value that arrives already
  // encoded (an accented name copied from a browser, e.g. "n%C3%A1tnat") is not encoded
  // twice into "%25C3%25A1", which the API would answer with 404.
  QString normaliseForRequest(const QString & value)
  {
    const QString decoded = QUrl::fromPercentEncoding(value.toUtf8()).toLower();
    return QString::fromUtf8(QUrl::toPercentEncoding(decoded));
  }

  // Blizzard and the proxy both report failures as small JSON objects: Blizzard sends
  // {"code":404,"type":"BLZWEBAPI...","detail":"Not Found"}, the proxy {"error":"..."}.
  QString errorTextFrom(const QJsonObject & json, const QByteArray & rawBody)
  {
    if (json.contains("detail"))
      return json.value("detail").toString();
    if (json.contains("error"))
      return json.value("error").toString();

    return QString::fromUtf8(rawBody).simplified().left(MAX_DETAIL_CHARS);
  }
}

namespace ArmoryProxy
{
  QString defaultProxyTemplate()
  {
    return DEFAULT_PROXY_URL;
  }

  QString redactSecrets(const QString & text)
  {
    // Qt's own network error strings quote the URL they failed on, so this has to run over
    // messages as well as over the URL we build ourselves.
    static const QRegularExpression secret(
      "([?&](?:key|token|secret|password|passwd|apikey|api_key|access_key)=)[^&\\s\"']*",
      QRegularExpression::CaseInsensitiveOption);

    QString safe = text;
    return safe.replace(secret, "\\1<hidden>");
  }

  Status parseCharacterUrl(const QString & url, Character & out)
  {
    out = Character();

    QString text = url.trimmed();
    if (text.isEmpty())
      return Status::BadLink;

    // Accept what a user can paste: with or without a scheme. Everything below works on
    // QUrl's parsed host/path/query rather than counting characters in the raw string, so
    // a trailing slash, an extra query or a changed prefix cannot shift the fields.
    if (!text.contains("://"))
      text.prepend("https://");

    const QUrl parsed(text);
    const QString host = parsed.host().toLower();
    // Keep the path percent-encoded while splitting: a decoded path could contain a '/'
    // of its own and split into the wrong number of segments.
    const QStringList segments = parsed.path(QUrl::FullyEncoded).split('/', QString::SkipEmptyParts);

    if (host.isEmpty() || segments.isEmpty())
      return Status::BadLink;

    // The site has used several shapes for a character page, all of which carry a
    // "character" segment followed by the realm and the name:
    //   /<locale>/worldsoul/<region>/armory/character/<realm>/<name>   (current)
    //   /<locale>/character/<region>/<realm>/<name>                    (2023)
    //   /<locale>/character/<realm>/<name>                             (older)
    //   /wow/<lang>/character/<realm>/<name>/simple                    (battle.net)
    int characterAt = -1;
    for (int i = 0; i < segments.size(); i++)
    {
      if (segments.at(i).compare("character", Qt::CaseInsensitive) == 0)
      {
        characterAt = i;
        break;
      }
    }

    if (characterAt < 0)
    {
      // No character segment. The Armory search page (".../armory?q=name") is the link
      // people most often copy by mistake, so name it instead of reporting a bad link.
      for (const auto & segment : segments)
      {
        if (segment.compare("armory", Qt::CaseInsensitive) == 0)
          return Status::SearchLink;
      }
      return Status::BadLink;
    }

    const int following = segments.size() - characterAt - 1;
    if (following >= 3 && isRegionToken(segments.at(characterAt + 1)))
    {
      // /character/<region>/<realm>/<name>
      out.region = segments.at(characterAt + 1);
      out.realm = segments.at(characterAt + 2);
      out.name = segments.at(characterAt + 3);
    }
    else if (following >= 2)
    {
      // /character/<realm>/<name>, with the region somewhere else in the link
      out.realm = segments.at(characterAt + 1);
      out.name = segments.at(characterAt + 2);
    }
    else
    {
      return Status::BadLink;
    }

    // Region named earlier in the path, as in the current ".../<region>/armory/character/..."
    if (out.region.isEmpty())
    {
      for (int i = characterAt - 1; i >= 0; i--)
      {
        if (isRegionToken(segments.at(i)))
        {
          out.region = segments.at(i);
          break;
        }
      }
    }

    // battle.net links name the region in the host: us.battle.net, eu.battle.net, ...
    if (out.region.isEmpty() && host.endsWith("battle.net"))
    {
      const QString subdomain = host.section('.', 0, 0);
      if (isRegionToken(subdomain))
        out.region = subdomain;
    }

    // Last resort: the locale segment at the front of the path.
    if (out.region.isEmpty())
      out.region = regionFromLocale(segments.at(0));

    out.region = out.region.toLower();
    out.realm = out.realm.toLower();

    if (out.realm.isEmpty() || out.name.isEmpty())
      return Status::BadLink;
    if (out.region.isEmpty())
      return Status::BadLink;
    if (!isRegionToken(out.region))
      return Status::UnsupportedRegion;

    return Status::Ok;
  }

  QString buildRequestUrl(const QString & proxyTemplate, const Character & character)
  {
    QString filled = proxyTemplate;
    const QString values[3] = { character.region, character.realm, character.name };

    // Substitute the first three "%s" placeholders in order (region, realm, character).
    for (const auto & value : values)
    {
      const int idx = filled.indexOf("%s");
      if (idx < 0)
        break;
      filled.replace(idx, 2, normaliseForRequest(value));
    }

    return filled;
  }

  bool looksLikeAppearance(const QJsonObject & json)
  {
    // An appearance document names the character and the race it plays. That is what stops
    // an error body -- valid JSON, but no character -- from being dressed onto the model as
    // a race-0 character with no equipment. The items array is NOT part of the test: a
    // character wearing nothing comes back without one.
    return json.value("character").isObject() &&
           json.value("playable_race").toObject().value("id").toInt() > 0;
  }

  Result fetchJson(const QString & requestUrl)
  {
    Result result;

    const QUrl url(requestUrl);
    if (!url.isValid())
    {
      result.status = Status::BadLink;
      result.detail = url.errorString();
      LOG_ERROR << "Armory: refusing to request an invalid URL:" << qPrintable(redactSecrets(result.detail));
      return result;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "WoWModelViewer");
    // Proxies are often reached through a redirect (http -> https, or a hosting redirect);
    // without this the reply would be a 30x with an empty body.
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    // The proxy is a normal, publicly-trusted HTTPS endpoint, so keep Qt's default
    // certificate verification (VerifyPeer) -- it authenticates the proxy and prevents an
    // on-path attacker from feeding us forged character data.

    QScopedPointer<QNetworkReply> reply(manager.get(request));
    QEventLoop eventLoop;
    QObject::connect(reply.data(), &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);

    // The import runs on the UI thread, so a proxy that accepts the connection and then
    // says nothing would hang the whole viewer. Give up after a while and report it.
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, [&reply, &eventLoop]()
    {
      reply->abort();
      eventLoop.quit();
    });
    timeout.start(REQUEST_TIMEOUT_MS);

    eventLoop.exec();

    const QByteArray body = reply->readAll();
    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (result.httpStatus == 0)
    {
      // Nothing came back: DNS, connection, TLS or timeout. Report the transport error
      // rather than an empty-JSON complaint that hides it.
      result.status = Status::NetworkError;
      // Qt's error string quotes the URL it failed on, access key and all.
      result.detail = redactSecrets(reply->errorString());
      LOG_ERROR << "Armory: could not reach the proxy:" << qPrintable(result.detail);
      return result;
    }

    LOG_INFO << "Armory: proxy answered HTTP" << result.httpStatus << "with" << body.size() << "bytes";

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    const QJsonObject json = document.object();

    if (result.httpStatus < 200 || result.httpStatus > 299)
    {
      result.detail = errorTextFrom(json, body);
      if (result.httpStatus == 404)
        result.status = Status::NotFound;
      else if (result.httpStatus == 401 || result.httpStatus == 403)
        result.status = Status::AccessDenied;
      else if (result.httpStatus == 429)
        result.status = Status::RateLimited;
      else
        result.status = Status::ServerError;

      LOG_ERROR << "Armory: proxy request failed, HTTP" << result.httpStatus
                << "-" << qPrintable(result.detail);
      return result;
    }

    if (document.isNull() || !document.isObject() || json.isEmpty())
    {
      result.status = Status::UnexpectedPayload;
      result.detail = parseError.error == QJsonParseError::NoError
        ? QString::fromUtf8(body).simplified().left(MAX_DETAIL_CHARS)
        : parseError.errorString();
      LOG_ERROR << "Armory: the proxy returned something that is not a JSON object:"
                << qPrintable(result.detail);
      return result;
    }

    result.json = json;
    return result;
  }

  QString describe(const Result & result, const Character & character)
  {
    const QString who = QString("%1 on %2 (%3)")
      .arg(QUrl::fromPercentEncoding(character.name.toUtf8()))
      .arg(character.realm)
      .arg(character.region);
    const QString tail = result.detail.isEmpty()
      ? QString()
      : QString("\n\nDetails: %1").arg(result.detail.left(MAX_DETAIL_CHARS));

    switch (result.status)
    {
      case Status::Ok:
        return QString();

      case Status::SearchLink:
        return "That link points at the Armory, but not at a character -- it is a search or "
               "listing page.\n\n"
               "Open the character itself, then copy the address of that page: it has "
               "/character/ in it, as in .../armory/character/<realm>/<name>.";

      case Status::BadLink:
        return "Could not read the character link.\n\n"
               "Paste the address of a character's Armory page, for example\n"
               "https://worldofwarcraft.blizzard.com/en-gb/worldsoul/eu/armory/character/silvermoon/natnat";

      case Status::UnsupportedRegion:
        return QString("Unsupported region \"%1\".\n\nOnly the US, EU, KR and TW regions are served.")
          .arg(character.region);

      case Status::NoProxy:
        return "No Armory proxy is configured.\n\n"
               "This build has no built-in proxy URL. Set one in Settings > General "
               "(Armory Proxy URL), or rebuild with a default proxy. See armory-proxy/README.md.";

      case Status::NetworkError:
        return QString("Could not reach the Armory proxy.\n\nCheck your internet connection and the "
                       "proxy URL in Settings > General.%1").arg(tail);

      case Status::NotFound:
        return QString("The Armory has no profile for %1.\n\n"
                       "Check the name, realm and region in the link. A character also has to have "
                       "been logged in recently, and its profile must not be hidden.%2").arg(who).arg(tail);

      case Status::AccessDenied:
        return QString("The Armory proxy refused the request for %1.\n\n"
                       "Its access key is wrong or has been changed. Check the Armory Proxy URL in "
                       "Settings > General.%2").arg(who).arg(tail);

      case Status::RateLimited:
        return QString("The Armory proxy is rate limited right now.\n\nWait a moment and try %1 again.%2")
          .arg(who).arg(tail);

      case Status::ServerError:
        return QString("The Armory proxy or Blizzard's API returned an error (HTTP %1) for %2.\n\n"
                       "This is usually temporary -- try again later.%3")
          .arg(result.httpStatus).arg(who).arg(tail);

      case Status::UnexpectedPayload:
        return QString("The Armory returned data this build does not understand for %1.\n\n"
                       "The proxy may be answering with something other than character appearance "
                       "data.%2").arg(who).arg(tail);
    }

    return QString("Could not retrieve character data from the Armory.%1").arg(tail);
  }
}
