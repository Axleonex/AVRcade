# Release Package Store

`ReleasePackageStore` is the title-neutral, local package foundation for VRClient-owned artifacts such as shared controller maps, UI assets, and future adapter/config bundles.

- It accepts an artifact only when its SHA-256 matches the caller-supplied manifest descriptor.
- It stages a verified copy before activating a version.
- It retains installed versions, checks active-artifact health, and can switch to the immediately previous version.
- It has no downloader, remote endpoint, game-install path, or adapter-specific behavior. Those are separate future integration layers.

The manifest schema is `config/schemas/release-package-manifest.schema.json`. Channels are `stable`, `nightly`, and `rollback`.
