// Host tool: prints the configuration reference (Markdown) from the
// built-in schema. Used to regenerate docs/config-reference.md.
#include <cstdio>
#include <string>

#include <lumina/ConfigSchema.h>
#include <lumina/Json.h>

using namespace lumina;

int
main()
{
	const ConfigSchema& schema = ConfigSchema::Builtin();
	printf("# Configuration reference\n\n"
		"Generated from the built-in schema (`src/core/lumina/ConfigSchemaData.cpp`)"
		" by `tools/schema_doc`. Every default reproduces stock Haiku: nothing "
		"changes until `session.mode` is `lumina` and a personality enables it.\n\n"
		"Paths are dotted; `*` is an array index. Settings marked *try-mode* "
		"are applied with an automatic revert unless confirmed.\n\n");
	std::string group;
	for (const SchemaEntry& e : schema.Entries()) {
		if (e.group != group) {
			group = e.group;
			printf("\n## %s\n\n| Setting | Type | Default | Description |\n"
				"|---|---|---|---|\n", group.c_str());
		}
		std::string type = SchemaTypeName(e.type);
		if (e.type == SchemaType::Enum) {
			type += ": ";
			for (size_t i = 0; i < e.values.size(); i++)
				type += (i ? " \\| " : "") + ("`" + e.values[i] + "`");
		}
		if (e.hasMin || e.hasMax) {
			char range[64];
			snprintf(range, sizeof(range), " (%g–%g)", e.min, e.max);
			type += range;
		}
		std::string def = e.defaultValue.IsNull() ? "—"
			: "`" + WriteJson(e.defaultValue, {false, 0}) + "`";
		printf("| `%s` | %s | %s | %s%s |\n", e.path.c_str(), type.c_str(),
			def.c_str(), e.description.c_str(), e.risky ? " *(try-mode)*" : "");
	}
	return 0;
}
