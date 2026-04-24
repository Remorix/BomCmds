# BomCmds
Function-to-function identical implementation of `lsbom`, `mkbom`, `ditto` for non-macOS

### Current Status

Aligned with macOS 13 shipped implementation (not sure what was the exact BomCmds version since newer versions has been migrated to non-Xcode builds which not carrying version strings).
Minimal target has not been tested yet, but should at least supporting Darwin 19 (macOS 10.14 / iOS 13), backports welcomed.

### TODO

- [ ] Standalone Bom.framework implementation
- [ ] CoreFoundation-less port for non-Darwin systems
