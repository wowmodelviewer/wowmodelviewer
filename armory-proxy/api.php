<?php
/*
 * WoW Model Viewer - Armory import proxy (PHP)
 * --------------------------------------------------------------------------
 * Drop-in for a shared/PHP host. Holds the Blizzard API credentials
 * server-side, performs the OAuth client_credentials grant + the WoW Profile
 * "appearance" fetch, and returns Blizzard's JSON unchanged.
 *
 * Client calls:  https://<host>/api.php?region=<region>&realm=<slug>&character=<name>
 * and, for its realm picker:  https://<host>/api.php?region=<region>&realms=1
 * which answers {"region":"eu","realms":[{"id":560,"slug":"stormscale","name":"Stormscale"},...]}
 * from the Game Data realm index, sorted by name and cached for a day.
 *
 * Credentials: set BLIZZARD_CLIENT_ID / BLIZZARD_CLIENT_SECRET as environment
 * variables (preferred -- SetEnv in the vhost, or your host's panel). A sibling
 * config.php is supported but should be placed OUTSIDE the web root or denied by
 * the server, since a misconfigured PHP handler would otherwise serve it as
 * plaintext and leak the secret. Never commit the secret.
 */

if (is_file(__DIR__ . '/config.php'))
    require __DIR__ . '/config.php';

header('Content-Type: application/json');

function fail($status, $msg) {
    http_response_code($status);
    echo json_encode(['error' => $msg]);
    exit;
}

$clientId = getenv('BLIZZARD_CLIENT_ID');
$clientSecret = getenv('BLIZZARD_CLIENT_SECRET');
if (!$clientId || !$clientSecret)
    fail(500, 'proxy misconfigured: missing Blizzard credentials');

// Optional shared-key gate (recommended). Constant-time compare.
$accessKey = getenv('ARMORY_ACCESS_KEY');
if ($accessKey && !hash_equals((string)$accessKey, (string)($_GET['key'] ?? '')))
    fail(401, 'unauthorized');

// Realm slugs can carry accented letters ("pozzo-delleternità"), so lower-case them
// the multibyte way when the extension is there.
function lower_utf8($s) {
    return function_exists('mb_strtolower') ? mb_strtolower($s, 'UTF-8') : strtolower($s);
}

$region = strtolower(trim($_GET['region'] ?? ''));
$realm = lower_utf8(trim($_GET['realm'] ?? ''));
$character = strtolower(trim($_GET['character'] ?? ''));
$wantsRealms = ($_GET['realms'] ?? '') === '1';

// region: retail (us/eu/kr/tw) or classic-/classic1x- prefixed. The /D flag pins
// '$' to the true end of string so a trailing newline can't slip through.
if (!preg_match('/^(classic1x-|classic-)?(us|eu|kr|tw)$/D', $region, $m))
    fail(400, 'invalid region');
$prefix = $m[1];
$base = $m[2];
$flavour = $prefix === 'classic-' ? 'classic-' : ($prefix === 'classic1x-' ? 'classic1x-' : '');
$namespace = "profile-$flavour$base";
$dynamicNamespace = "dynamic-$flavour$base";
$localeMap = ['us' => 'en_US', 'eu' => 'en_GB', 'kr' => 'ko_KR', 'tw' => 'zh_TW'];
$locale = $localeMap[$base];
$host = "$base.api.blizzard.com";

// A realm slug as Blizzard spells it: letters (accented ones included), digits, hyphens.
function is_realm_slug($s) {
    return is_string($s) && preg_match('/^[\p{L}\p{N}-]{1,64}$/uD', $s) === 1;
}

// values are interpolated into the upstream path -> guard against SSRF / injection
// (the realm list route takes neither)
if (!$wantsRealms) {
    if (!is_realm_slug($realm))
        fail(400, 'invalid realm slug');
    if (strlen($character) < 1 || strlen($character) > 32 || preg_match('#[/\s?\#]#', $character))
        fail(400, 'invalid character name');
}

/*
 * OAuth token, cached to a private, per-deployment temp file.
 * Hardened against shared-/tmp attacks: the filename is unguessable (derived from
 * the client id), symlinks are refused, the file is created 0600, and writes are
 * atomic (write to a unique temp file, then rename into place). The token is NOT
 * the client secret but grants the same API access until it expires, so it is
 * treated as sensitive.
 */
function get_token($clientId, $clientSecret) {
    $cacheFile = sys_get_temp_dir() . '/wmv_bnet_' . hash('sha256', $clientId) . '.json';

    if (is_file($cacheFile) && !is_link($cacheFile)) {
        $c = json_decode(@file_get_contents($cacheFile), true);
        if (is_array($c) && isset($c['value'], $c['expiresAt'])
            && is_string($c['value']) && is_int($c['expiresAt'])
            && $c['expiresAt'] > time() + 60)
            return $c['value'];
    }

    $ch = curl_init('https://oauth.battle.net/token');
    curl_setopt_array($ch, [
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_POST => true,
        CURLOPT_USERPWD => "$clientId:$clientSecret",
        CURLOPT_HTTPAUTH => CURLAUTH_BASIC,
        CURLOPT_POSTFIELDS => 'grant_type=client_credentials',
        CURLOPT_TIMEOUT => 20,
    ]);
    $resp = curl_exec($ch);
    $code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    curl_close($ch);
    if ($code !== 200)
        return null;

    $data = json_decode($resp, true);
    if (empty($data['access_token']))
        return null;

    $ttl = min((int)($data['expires_in'] ?? 3600), 86400);
    $payload = json_encode(['value' => $data['access_token'], 'expiresAt' => time() + $ttl]);

    // atomic + private write
    $tmp = @tempnam(sys_get_temp_dir(), 'wmvtok');
    if ($tmp !== false) {
        @chmod($tmp, 0600);
        if (@file_put_contents($tmp, $payload) !== false)
            @rename($tmp, $cacheFile);
        else
            @unlink($tmp);
    }
    return $data['access_token'];
}

// Private, atomic cache write (same hardening as the token cache above). A temp file that
// cannot be moved into place is removed rather than left behind.
function write_private($file, $payload) {
    $tmp = @tempnam(sys_get_temp_dir(), 'wmvc');
    if ($tmp === false)
        return;
    @chmod($tmp, 0600);
    if (@file_put_contents($tmp, $payload) === false || !@rename($tmp, $file))
        @unlink($tmp);
}

// The realm picker's list: the region's realm index reduced to id/slug/name, sorted by
// name, cached for a day so a region costs one upstream call a day. The cache is checked
// before any token is asked for, so a cached list is served even while the OAuth grant
// fails. The file name includes the PHP user, so pools sharing /tmp never share a file.
$realmCacheFile = sys_get_temp_dir() . '/wmv_realms_'
    . hash('sha256', getmyuid() . "|$clientId|$dynamicNamespace|$locale") . '.json';
if ($wantsRealms && is_file($realmCacheFile) && !is_link($realmCacheFile)
    && filemtime($realmCacheFile) > time() - 86400) {
    $cached = @file_get_contents($realmCacheFile);
    if ($cached !== false && json_decode($cached, true) !== null) {
        header('Cache-Control: public, max-age=86400');
        echo $cached;
        exit;
    }
}

$token = get_token($clientId, $clientSecret);
if (!$token)
    fail(502, 'failed to obtain Blizzard OAuth token (check proxy credentials)');

if ($wantsRealms) {
    $ch = curl_init("https://$host/data/wow/realm/index?namespace=$dynamicNamespace&locale=$locale");
    curl_setopt_array($ch, [
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_HTTPHEADER => ["Authorization: Bearer $token"],
        CURLOPT_TIMEOUT => 25,
    ]);
    $body = curl_exec($ch);
    $code = curl_getinfo($ch, CURLINFO_HTTP_CODE) ?: 502;
    curl_close($ch);

    if ($body === false)
        fail(502, 'upstream error');
    if (strlen($body) > 2 * 1024 * 1024)
        fail(502, 'upstream response too large');
    if ($code !== 200) { // pass Blizzard's answer through
        http_response_code($code);
        echo $body;
        exit;
    }

    $index = json_decode($body, true);
    if (!is_array($index))
        fail(502, 'upstream returned invalid JSON');

    $realms = [];
    foreach (($index['realms'] ?? []) as $x) {
        $slug = $x['slug'] ?? '';
        if (!is_realm_slug($slug))
            continue;
        $name = $x['name'] ?? '';
        if (is_array($name))
            $name = $name[$locale] ?? ($name['en_US'] ?? '');
        $realms[] = ['id' => (int)($x['id'] ?? 0), 'slug' => $slug, 'name' => ($name !== '' ? $name : $slug)];
    }
    usort($realms, function ($a, $b) { return strcasecmp($a['name'], $b['name']); });

    $out = json_encode(['region' => $region, 'realms' => $realms], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    if ($out === false)
        fail(502, 'could not encode the realm list');
    write_private($realmCacheFile, $out);
    header('Cache-Control: public, max-age=86400');
    echo $out;
    exit;
}

$apiUrl = "https://$host/profile/wow/character/" . rawurlencode($realm) . '/' . rawurlencode($character) . '/appearance'
        . "?namespace=$namespace&locale=$locale";

$ch = curl_init($apiUrl);
curl_setopt_array($ch, [
    CURLOPT_RETURNTRANSFER => true,
    CURLOPT_HTTPHEADER => ["Authorization: Bearer $token"],
    CURLOPT_TIMEOUT => 25,
]);
$body = curl_exec($ch);
$code = curl_getinfo($ch, CURLINFO_HTTP_CODE) ?: 502;
curl_close($ch);

if ($body === false)
    fail(502, 'upstream error');
if (strlen($body) > 512 * 1024) // appearance JSON is small; reject absurd sizes
    fail(502, 'upstream response too large');

// pass Blizzard's status + body through unchanged
http_response_code($code);
echo $body;
