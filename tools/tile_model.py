from dataclasses import dataclass, field


@dataclass
class TilesetMeta:
    source: str = ""
    cols: int = 1
    rows: int = 1
    tile_width: int = 0
    tile_height: int = 0
    padding: int = 0


@dataclass
class TileDef:
    index: int
    col: int
    row: int
    properties: dict = field(default_factory=dict)
    bitmask: list | None = None  # 3x3 list of lists, or None


@dataclass
class GroupDef:
    name: str
    tile_indices: list[int] = field(default_factory=list)


@dataclass
class TilesetProject:
    meta: TilesetMeta = field(default_factory=TilesetMeta)
    tiles: list[TileDef] = field(default_factory=list)
    groups: list[GroupDef] = field(default_factory=list)
