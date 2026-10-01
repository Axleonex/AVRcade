# Supply-Chain Signing

VRClient treats delivered runtime, adapter, and config artifacts as untrusted
until two independent checks pass:

1. The artifact bytes match the SHA-256 recorded in the manifest.
2. The manifest entry has a valid signature that chains to the configured trust root.

Phase 8 distribution is not built yet, so B6 ships a standalone verifier and
schema-guarded fixtures:

- `config/supply-chain/trust-root.test.json`
- `config/supply-chain/manifest.test.json`
- `config/supply-chain/revocations.*.test.json`

The test root uses RSA PKCS#1 v1.5 with SHA-256 (`rsa-pkcs1-sha256`). Production
keys must be generated and held outside the client repository; private keys must
never ship with the app. The checked-in key is a public test key only, used to
prove the client-side verifier and safety integration.

Revocation is data, not code. A delivered revocation list can revoke either a
signing key or a specific artifact hash. Revoked or unsigned artifacts are passed
to the Phase 7 safety model as supply-chain trust observations and resolve to a
`block` verdict before any launch or injection path can proceed.

Rollback and pinned-build behavior remains compatible with signing: an older
manifest can still load when its artifact hash and signature verify and neither
the signing key nor the artifact hash appears on the active revocation list.
