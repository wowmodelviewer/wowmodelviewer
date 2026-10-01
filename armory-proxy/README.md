# WoW Model Viewer — Armory import proxy

The character importer in WMV never holds Blizzard API credentials. Instead it
calls a tiny **proxy you host**, which keeps the secret server-side, performs the
OAuth handshake, and returns the character's appearance JSON. Your end users
register nothing and paste nothing.

You register **one** Blizzard API client, deploy the proxy once, and bake its URL
into the build. Done.

The proxy URL format (used by both the Worker and PHP versions):

```
https://<your-proxy>/?region=%s&realm=%s&character=%s
```

`%s` placeholders are region, realm-slug, character-name (the client fills them in,
URL-encoded).

The import dialog also asks the same URL for a region's **realm list**, so people can
pick a realm instead of knowing its slug. It fills the region, leaves realm and
character empty and adds `&realms=1`:

```
https://<your-proxy>/?region=eu&realm=&character=&realms=1&key=<ACCESS_KEY>
```

The proxy answers from Blizzard's Game Data realm index (`/data/wow/realm/index`,
namespace `dynamic-<region>`) with `{"region":"eu","realms":[{"id":560,"slug":"stormscale","name":"Stormscale"}, …]}`,
sorted by name and cached for a day, so a region costs one upstream call a day however
many people open the dialog. The client caches each region's list for a week. A proxy
deployed before this route existed answers the request with `400 invalid realm slug`;
the dialog then falls back to typing the realm, so **redeploy the proxy** to get the
realm picker.

Realm slugs may contain accented letters (`pozzo-delleternità`, `festung-der-stürme`,
`aggra-português`, …). The proxy accepts any letters, digits and hyphens and
percent-encodes the slug on the way upstream; earlier versions accepted only ASCII and
could not import characters on those realms at all.

> **This single endpoint spends your one Blizzard quota.** It is public by design
> (the URL ships inside the app), so treat it as a shared credential: set an
> `ACCESS_KEY` and add a rate limit (steps below). Blizzard caps a client to roughly
> 100 requests/second and 36,000/hour — without a guard, anyone who finds the URL can
> exhaust that and get your client throttled.

---

## Step 1 — Register one Blizzard API client (once)

1. Go to <https://develop.battle.net> and log in with your Battle.net account.
2. **Create Client**. Any name (e.g. "WMV armory proxy"). No redirect URI needed.
3. Copy the **Client ID** and **Client Secret**. These go on your server only —
   never in the WMV repo or the shipped binary.

---

## Step 2 — Deploy the proxy

### Option A — Cloudflare Worker (recommended; free)

1. Install Wrangler and log in:
   ```
   npm install -g wrangler
   wrangler login
   ```
2. From this folder, deploy the Worker (uses `cloudflare-worker.js`):
   ```
   wrangler deploy cloudflare-worker.js --name wmv-armory
   ```
3. Set the credentials and an access key as **secrets** (encrypted, never in code):
   ```
   wrangler secret put BLIZZARD_CLIENT_ID
   wrangler secret put BLIZZARD_CLIENT_SECRET
   wrangler secret put ACCESS_KEY        # recommended; pick any random string
   ```
4. **Add a rate limit** (required for a public endpoint): in the Cloudflare dashboard,
   Security → WAF → Rate limiting rules, add a rule on the Worker route (e.g. 60
   requests/minute per IP).
5. Your proxy URL is (append the key if you set one):
   ```
   https://wmv-armory.<your-subdomain>.workers.dev/?region=%s&realm=%s&character=%s&key=<ACCESS_KEY>
   ```

### Option B — PHP host (shared hosting)

1. Upload `api.php` to your web host.
2. Provide credentials as **environment variables** (preferred):
   `BLIZZARD_CLIENT_ID`, `BLIZZARD_CLIENT_SECRET`, and optionally `ARMORY_ACCESS_KEY`
   (via `SetEnv` in the vhost or your host's control panel).
   - If you must use a `config.php` instead, **place it outside the web root** (and
     `require` it by absolute path), or deny access to it in your server config
     (e.g. `<Files config.php>Require all denied</Files>`). Otherwise a misconfigured
     PHP handler could serve `config.php` as plaintext and leak your secret.
3. Your proxy URL is:
   ```
   https://<your-host>/api.php?region=%s&realm=%s&character=%s&key=<ACCESS_KEY>
   ```

### Verify it works
```
curl "https://<your-proxy>/?region=eu&realm=<realm-slug>&character=<a character you know>&key=<ACCESS_KEY>"
```
You should get the appearance JSON (playable_race, gender, items, customizations…).

```
curl "https://<your-proxy>/?region=eu&realms=1&key=<ACCESS_KEY>"
```
should return the realm list (`{"region":"eu","realms":[…]}`).

---

## Step 3 — Bake the URL into WMV

Open `Source/plugins/importers/armory/ArmoryProxy.cpp`, set:

```cpp
const QString DEFAULT_PROXY_URL = "https://<your-proxy>/?region=%s&realm=%s&character=%s&key=<ACCESS_KEY>";
```

Rebuild (`_build.bat`) and redeploy (`_run.bat`). Now every user's import "just
works" with no setup. (Power users can override the URL at runtime in
**Settings → General → Armory → Proxy URL** without rebuilding.)

The proxy URL (including the access key) is **not** a true secret — it is embedded in
the shipped binary. It exists to block trivial scraping and to let you rotate access;
the rate limit is your real protection. The Blizzard **client secret** stays only on
your server.

---

## Repo hygiene

- Commit `armory-proxy/.gitignore` **first** — it ignores `config.php`, `.dev.vars`,
  `wrangler.toml`, and `.wrangler/`.
- Never `git add config.php` / `wrangler.toml` / `.dev.vars` explicitly (that bypasses
  ignore rules). Run `git status` before committing to confirm no secret is staged.

## Security notes

- The client secret lives only on your server. The shipped binary contains just the
  proxy URL, so there is nothing to extract from it.
- The proxy only exposes **public** WoW armory appearance data (the same data the
  in-game armory shows). It cannot touch private account data.
- Inputs (region/realm/character) are validated before use, so the proxy can't be
  tricked into calling arbitrary hosts. Upstream calls are bounded by a timeout and a
  response-size cap.
- The OAuth token is cached privately (Worker isolate memory; PHP: a 0600,
  unguessable, atomically-written temp file).
