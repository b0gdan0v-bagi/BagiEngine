# Skill deploy-editor: the dry run shows the upload

- Version: 0.30.3
- Entry: deploy-editor-dry-run-upload
- Date: 2026-11-27
- Bump: patch

## Changed
- The skill `deploy-editor` runs its dry run as `--target <name> --deploy --dry-run`, so the plan names the site, the page, the upload and where the token comes from; `tools/web/DEPLOY.md` says a site page's upload lines appear only with `--deploy`. The script is unchanged. Adapt: nothing.
