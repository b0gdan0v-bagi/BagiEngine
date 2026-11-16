# Skill `/artgen`: an agent generates and accepts art and sounds through the editor

- Version: 0.1.14
- Entry: artgen-skill
- Date: 2026-11-16
- Bump: patch

## Added
- `.claude/skills/artgen/SKILL.md`: starts ComfyUI, the editor (`--editor --control`) and the bridge when they are not running, picks a preset or sends a draft, generates over `artgen_*`, judges the image variants itself, leaves the choice of a sound to a person, accepts into the game (a sprite, a WAV clip), makes sets with one seed, and closes what it started. A game links it like the other shared skills.
