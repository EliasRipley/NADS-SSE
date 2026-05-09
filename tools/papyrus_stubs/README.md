Papyrus fallback stubs for local compilation.

Why these exist:
- Some build environments only have a partial Skyrim script source set.
- `tools/CompilePapyrus.ps1` appends this folder as the last import path.
- Real script sources from `ImportRoot` take precedence; these are only fallback definitions.

What this is for:
- Allow compiling NADS scripts in environments missing parts of the vanilla source package.
- Keep build-time compatibility shims out of `Data/Scripts/Source`.
