"""What `schema_to_xsd.py` and `export_schema.py` share: the schema's version, the names the XML serializers write, the XSD's own names.

The XML names are those of Reflection/DataAccessor.h and XmlSerializer.cpp (a list is `<field><item/>...</field>`, a map is
`<field><entry key=""/>...</field>`, a variant is `index` and `value`, a factory element carries `type` and `enabled`). Standard library only.
"""

SCHEMA_VERSION = 1  # `version` of the answer of the control method `schema` (SchemaExport.h, kSchemaVersion)
SCHEMA_METHOD = "schema"

XS_NAMESPACE = "http://www.w3.org/2001/XMLSchema"
XS_PREFIX = "xs"
XS_ANY_TYPE = f"{XS_PREFIX}:anyType"
XS_STRING = f"{XS_PREFIX}:string"
XS_BOOLEAN = f"{XS_PREFIX}:boolean"
XS_INTEGER = f"{XS_PREFIX}:int"
XS_UNSIGNED_INTEGER = f"{XS_PREFIX}:nonNegativeInteger"

# The kinds of a field that are one XML attribute, and the XSD type each is read as (`TypeKindName` of the registry)
SCALAR_XSD_TYPES = {
    "bool": XS_BOOLEAN,
    "int8": f"{XS_PREFIX}:byte",
    "int16": f"{XS_PREFIX}:short",
    "int32": XS_INTEGER,
    "int64": f"{XS_PREFIX}:long",
    "uint8": f"{XS_PREFIX}:unsignedByte",
    "uint16": f"{XS_PREFIX}:unsignedShort",
    "uint32": f"{XS_PREFIX}:unsignedInt",
    "uint64": f"{XS_PREFIX}:unsignedLong",
    "float": f"{XS_PREFIX}:float",
    "double": f"{XS_PREFIX}:double",
    "string": XS_STRING,
}
INTEGER_KINDS = frozenset(kind for kind in SCALAR_XSD_TYPES if kind.startswith(("int", "uint")))

ITEM_ELEMENT = "item"  # a list that is not a factory list
ENTRY_ELEMENT = "entry"
KEY_ATTRIBUTE = "key"
VALUE_NAME = "value"  # the attribute of a scalar element, and the member of a variant that holds the alternative
INDEX_ATTRIBUTE = "index"
TYPE_ATTRIBUTE = "type"
ENABLED_ATTRIBUTE = "enabled"
PAIR_FIRST = "first"
PAIR_SECOND = "second"

# A content file is read after the loader has taken what is its own (BECore/Config/ContentVersion.h, Scene/SceneNodeFiles.h)
CONTENT_VERSION_ATTRIBUTE = "contentVersion"
CONTENT_ROOTS = ("SceneManager", "node")
NODE_REFERENCE_ATTRIBUTE = "ref"
NODE_TYPE = "SceneNode"

CONFIG_EXTENSION = ".xml"
XSD_EXTENSION = ".xsd"

# The exit codes of the tools and of tools/control/tests/smoke.py
EXIT_PASSED = 0
EXIT_FAILED = 1
EXIT_BAD_USAGE = 2
EXIT_SKIPPED = 77
