# DiskDude Design Direction

## Identity & Purpose
- **Product**: DiskDude, a native Linux and Windows disk-space analyzer, duplicate finder, app leftover cleaner, system monitor, and snapshot comparison tool.
- **Audience**: Developers, power users, and desktop users on Linux and Windows who want fast, honest visibility into storage consumption without risking accidental permanent deletion.
- **Design Read**: Native desktop disk-space analyzer utility for Linux and Windows users, in a high-contrast tactile utility style, dial `ENERGY 2 / RHYTHM 2 / MOTION 1`.
- **Dial**: `ENERGY 2 / RHYTHM 2 / MOTION 1`

## Color System & Contrast Verification (WCAG AA >= 4.5:1)

### Light Theme
- `background`: `#FFFFFF`
- `panel`: `#F5F5F7`
- `card`: `#FFFFFF`
- `border`: `#D8D8DD` (reinforced with `#8E8E95` on interactive component boundaries for >= 3:1 non-text contrast)
- `divider`: `#E5E5EA`
- `chip`: `#F2F2F7`
- `track`: `#E5E5EA`
- `text`: `#1D1D1F` (16.83:1 on `#FFFFFF`, 15.42:1 on `#F5F5F7`)
- `textSecondary`: `#525257` (7.64:1 on `#FFFFFF`, 7.00:1 on `#F5F5F7`)
- `textTertiary`: `#595960` (6.80:1 on `#FFFFFF`, 6.23:1 on `#F5F5F7`)
- `ink` (accent fill): `#FF6B4A`
- `onInk` (text on accent fill): `#0F172A` (6.65:1 on `#FF6B4A`, meeting WCAG AA normal text)
- `used`: `#C92A2A` text / `#FF3B30` bar
- `free`: `#1B7A34` text / `#34C759` bar

### Dark Theme
- `background`: `#1E1E20`
- `panel`: `#28282B`
- `card`: `#2C2C2E`
- `border`: `#3F3F43` (reinforced with `#787880` on interactive controls for >= 3:1 non-text contrast)
- `divider`: `#38383A`
- `chip`: `#3A3A3C`
- `track`: `#3A3A3C`
- `text`: `#F5F5F7` (15.29:1 on `#1E1E20`)
- `textSecondary`: `#AEAEB2` (7.53:1 on `#1E1E20`)
- `textTertiary`: `#9E9EA4` (6.25:1 on `#1E1E20`, 5.14:1 on `#2C2C2E`)
- `ink` (accent fill): `#FF7A5C`
- `onInk` (text on accent fill): `#0F172A` (7.45:1 on `#FF7A5C`, meeting WCAG AA normal text)
- `used`: `#FF6B63` text / `#FF453A` bar
- `free`: `#4ADE80` text / `#30D158` bar

### Data Palette (Pastel Category & Age Fills)
- `Video`: `#FDA4AF`
- `Audio`: `#C4B5FD`
- `Image`: `#7DD3FC`
- `Document`: `#FCD34D`
- `Developer`: `#86EFAC`
- `Archive`: `#FDBA74`
- `Apps`: `#93C5FD`
- `System`: `#5EEAD4`
- `Other`: `#CBD5E1`
- `Age Buckets`: `#34D399`, `#60A5FA`, `#818CF8`, `#A78BFA`, `#F472B6`, `#F87171`
- `Text on Pastel Fills`: Primary `#0F172A` (5.98:1 to 12.71:1 across all pastel fills) and Secondary `#1E293B` (4.90:1 to 10.42:1 across all pastel fills).

## One-Line Decision Ledger (R-31)
- **Color Palette**: High-luminance pastel data fills (`#FDA4AF` to `#CBD5E1`) paired with `#0F172A` dark slate ink allow dense multi-level charts (Treemap, Sunburst, Flame) to stay readable without dark-mode muddying, while coral ink (`#FF6B4A` / `#FF7A5C`) marks primary scan actions and active navigation.
- **Layout Structure**: A three-pane workbench (Sidebar for scan targets and Quick Wins, central viewport for the 9 visualizations, collapsible right Inspector for file metadata and staging) keeps navigation, spatial discovery, and file inspection visible simultaneously without modal context switching.
- **Responsive Reflow**: The workbench adapts across three width breakpoints (`>= 1280px` three-pane, `860-1279px` two-pane with icon-condensed tabs and wrapped toolbar, `< 860px` single-column compact layout) so narrow windows never clip controls or leak horizontal scroll.
- **Typography**: System UI sans-serif handles dense labels and tables cleanly, while monospace is reserved strictly for filesystem paths, hashes, PIDs, and byte counters so path hierarchy and numeric alignment are scannable.
- **Spacing & Radii**: Continuous 8-12px radii on panels and cards distinguish interactive containers from 4px data cells inside treemaps and flame charts.
- **Folder Cards**: Custom top-tabbed folder cards in the Folders view visually separate navigable directories from flat leaf files below them.
- **Vector Icons**: Purpose-drawn geometric glyphs represent specific file types, storage volumes, and actions without decorative emoji or generic AI sparkle icons.
- **Motion**: Dials are set to `MOTION 1` (instant hover highlights in a dedicated overlay layer and crisp state switches) so mouse movement over 10,000+ treemap rectangles never lags or triggers distracting loops.
