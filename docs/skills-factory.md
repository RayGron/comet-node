# SkillsFactory

## Summary

`SkillsFactory` is the controller-owned canonical catalog for Skills in `comet-node`.

- Canonical skill content is stored once in controller SQLite.
- Plane `skills-<plane>` SQLite remains a runtime copy used by request-time resolution.
- Plane selection uses snapshot semantics through `desired-state.v2.skills.factory_skill_ids[]`.
- Plane-local binding metadata stays plane-scoped:
  - `enabled`
  - `session_ids[]`
  - `comet_links[]`

## Controller APIs

Global catalog:

- `GET /api/v1/skills-factory`
- `POST /api/v1/skills-factory`
- `GET /api/v1/skills-factory/<skill_id>`
- `PUT /api/v1/skills-factory/<skill_id>`
- `PATCH /api/v1/skills-factory/<skill_id>`
- `DELETE /api/v1/skills-factory/<skill_id>`

Plane-scoped catalog:

- `GET /api/v1/planes/<plane>/skills`
- `POST /api/v1/planes/<plane>/skills`
- `GET /api/v1/planes/<plane>/skills/<skill_id>`
- `PUT /api/v1/planes/<plane>/skills/<skill_id>`
- `PATCH /api/v1/planes/<plane>/skills/<skill_id>`
- `DELETE /api/v1/planes/<plane>/skills/<skill_id>`

Model Library:

- `GET /api/v1/model-library`
  - each model item now includes `skills_factory_worker`
- `POST /api/v1/model-library/skills-factory-worker`
  - body: `{ "path": "<absolute-model-path>" }`

## Desired State Contract

`desired-state.v2.skills` extends to:

```json
{
  "enabled": true,
  "factory_skill_ids": ["skill-alpha", "skill-beta"]
}
```

Rules:

- `factory_skill_ids[]` is valid only when `skills.enabled=true`
- items must be unique non-empty strings
- `Skills` remains `llm`-only

## Behavior

### SkillsFactory CRUD

- Create or update in `SkillsFactory` changes canonical content immediately.
- `GET /api/v1/skills-factory` returns:
  - `id`
  - `name`
  - `description`
  - `content`
  - `created_at`
  - `updated_at`
  - `plane_names[]`
  - `plane_count`

### Plane-scoped CRUD

- Plane create/edit for a skill writes canonical content through to `SkillsFactory`.
- The current plane is attached to the same `skill_id`.
- Plane delete is detach-only:
  - remove plane binding
  - remove the id from that plane’s `factory_skill_ids[]`
  - keep the canonical `SkillsFactory` record

### Factory delete

Deleting from `SkillsFactory` is global detach:

- remove the canonical record
- remove plane bindings for that `skill_id`
- remove the id from every plane’s `factory_skill_ids[]`
- best-effort delete the live runtime copy from ready planes
- offline or unready planes converge on the next reconciliation

### Runtime synchronization

`PlaneSkillRuntimeSyncService` materializes selected factory skills into plane-local runtime state:

- create/update/start reconciliation syncs selected skills into `skills-<plane>`
- deselection removes runtime copies no longer selected
- interaction-time resolution still reads only the plane-local runtime copy

## Operator UI

### Sidebar

The main operator sidebar now includes `Skills Factory`.

### Skills Factory page

The page supports:

- create, edit, and delete canonical skills
- visibility of:
  - `id`
  - `name`
  - `description`
  - `plane_names`
  - `plane_count`
- sorting by:
  - `name`
  - `plane_count`
- dynamic search across all visible fields

### Plane editor

When `Skills` is enabled for an `llm` plane:

- a `Skills Factory` selector table appears under `Features`
- selected records persist into `desired-state.v2.skills.factory_skill_ids[]`
- rollout or restart syncs those skills into the plane runtime copy

### Models page

Models page supports a single `Skills Factory Worker` designation:

- set by the hardhat action button
- exactly one model can hold the designation at a time
- selecting a new model replaces the previous designation
- this flag is persisted and displayed in this stage only
- it has no runtime behavior yet
