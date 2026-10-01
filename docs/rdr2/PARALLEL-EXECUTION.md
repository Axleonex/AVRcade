# RDR2 parallel-execution contract

RDR2 is independent from the deferred RDR1 adapter. Each run uses a title and
run-scoped state root, lock name, IPC endpoint, staging root, transaction root,
and evidence root. No global converter mutex is permitted. When an OpenXR
runtime cannot focus another session, the contender reports
`waiting_for_xr_focus` and stays non-modifying; process lifetime remains
independent from XR focus ownership.
