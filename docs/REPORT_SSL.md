# Tezos chain read: SSL failure report

## Symptom

The Tezos chain read (`ofApp::tezosThreadFunc`) polls TzKT over HTTPS and fails on every poll. The error is a TLS/SSL certificate failure, not SSH, and it gets logged as `Tezos: poll failed: ...`. No chain drawings are ever cached.

## Root cause

1. `ofxHTTP::Client` gets its TLS settings from `ofSSLManager::initializeClient()`, which looks for a CA bundle in two places:
   - `bin/data/ssl/cacert.pem`: missing in this app.
   - `addons/ofxSSLManager/shared/data/ssl/cacert.pem`: also missing. That folder only has `ssl.sh`, a download script that has never been run.
2. When neither file is found, it logs `CA File not found` and creates `Poco::Net::Context(CLIENT_USE, "")`. In the installed Poco (`libpoco-dev 1.11.0-3+deb12u1`), `loadDefaultCAs` defaults to `false`, so the context trusts no certificates at all.
3. TzKT's certificate chain is `api.shadownet.tzkt.io` → Let's Encrypt YR1 → ISRG Root YR → ISRG Root X1. With an empty trust store, verification fails and the handshake is rejected.

## Evidence

- `curl https://api.shadownet.tzkt.io/v1/contracts/KT1DypSEV87pwiw6swdYqhDKWRBZ7xfqeS3c/bigmaps` returns HTTP 200, so the network and the server are fine.
- `openssl s_client` using the system CA store: `Verify return code: 0 (ok)`.
- `openssl s_client -no-CAfile -no-CApath -no-CAstore` reproduces the failure: `verify error:num=20:unable to get local issuer certificate`.

## Fix applied

In `src/ofApp.cpp`, `ofApp::setup()` now sets up the client TLS context with the OS trust store before starting the Tezos thread:

```cpp
    if (!tezosContract.empty()) {
        // No bin/data/ssl/cacert.pem ships with the app, and ofSSLManager's fallback
        // context trusts nothing. Use the OS trust store instead.
        ofSSLManager::initializeClient(new Poco::Net::Context(
            Poco::Net::Context::TLS_CLIENT_USE, "",
            Poco::Net::Context::VERIFY_RELAXED, 9, true /* loadDefaultCAs */));

        tezosRunning = true;
        tezosThread = std::thread(&ofApp::tezosThreadFunc, this);
    }
```

The system bundle (`/etc/ssl/certs/ca-certificates.crt`) gets updated through Debian packages, so the app no longer depends on a data file that may be missing or out of date.

### Alternatives considered

- Symlink the system bundle into the app: `mkdir -p bin/data/ssl && ln -s /etc/ssl/certs/ca-certificates.crt bin/data/ssl/cacert.pem`.
- Run `addons/ofxSSLManager/shared/data/ssl.sh` from `setup.sh` to download `cacert.pem`. The copy in ofxHTTP's examples dates from March 2018. It happens to include ISRG Root X1, but it will go stale.

## Status

- **Build:** `make` succeeds (exit 0, no compiler errors). `bin/PiNaplpsPlayer` was rebuilt after the change.
- **Runtime:** verified. The app ran headless for 45 s under Xvfb:
  `cd bin && timeout 45 xvfb-run -a -s "-screen 0 1280x720x24 +extension GLX" ./PiNaplpsPlayer`
  Both polls in that window succeeded with no SSL or `poll failed` warnings:
  ```
  [notice ] Tezos: cached 100 drawings from chain
  [notice ] Tezos: cached 100 drawings from chain
  ```
  (Exit code 124 means `timeout` stopped the app, as intended.)
- **Side note:** 100 is exactly the `limit=100` used in the `/bigmaps/{ptr}/keys` query. If the contract holds more than 100 active keys per big_map, the rest are never fetched. That limit has nothing to do with the SSL problem.
- The change is not committed, and `ARCHITECTURE.md` has not been updated to mention the CA setup.
