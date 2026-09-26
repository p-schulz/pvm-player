# glad (vendored)

Pre-generated OpenGL 3.3 core loader (no extensions), generated once via the
`glad2` CLI (`pip install glad2`, then
`glad --api gl:core=3.3 --extensions "" --out-path . c`) and committed here
so the build doesn't need Python/network access to regenerate it.

Regenerate only if the required GL version/profile changes.
