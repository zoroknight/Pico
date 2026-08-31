# Pico Component Icon Masters

These files are deterministic editor-only icon masters. They are not `/Game`
assets and must not be staged into packaged projects.

## Contract

- Canvas: `128 x 128`, transparent.
- Stroke: `8 px`, rounded caps and joins.
- Master color: `#F2F5F3`; apply semantic color in the viewport renderer.
- Nominal display size: `80 px`; selected size: `92 px`, DPI scaled.
- Minimum picking area: `80 px`.
- Keep icons at a constant screen-space size.
- Hover, selected, disabled, and error states are renderer-driven, not separate assets.

## Suggested viewport tints

| Component | Tint |
|---|---|
| Point Light | `#F2B45E` |
| Directional Light | `#E6CD63` |
| Camera | `#69BDE0` |
| Player Start | `#69CC8A` |
| Spring Arm | `#A98ADE` |

All five icons are integrated as pickable viewport billboards. Their detailed
wire helpers appear only while selected; Spring Arm keeps its arm-length and
endpoint helper so attachment geometry remains inspectable.

The SVG files are the source of truth. The sibling PNG files are generated at
`128 x 128` for the current raster loading path.
