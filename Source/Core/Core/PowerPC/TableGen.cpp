#include <bitset>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

struct Instruction
{
  std::string opname;
  std::string type;
  std::string flags;
  std::deque<std::string> dispatch;
  Instruction(std::string opname_, std::string type_, std::string flags_,
              std::deque<std::string> dispatch_)
      : opname(opname_), type(type_), flags(flags_), dispatch(dispatch_)
  {
  }
};

struct SubTable
{
  std::string name;
};

struct StartTableMarker
{
  std::string name;
  int shift;
  int length;
  std::string description;
};
struct Empty
{
};

using InputLine = std::variant<Instruction, SubTable, StartTableMarker, Empty>;

constexpr char SEPARATOR = ';';

enum
{
  OPID_RANGES = 1,
  DECODING_TABLE = 2,
  OPINFO_TABLE = 4,
};

struct ColumnOptions
{
  unsigned int column;
  std::string prefix;
  std::string default_;
};

struct OutputOptions
{
  int flags = 0;
  std::vector<ColumnOptions> columns;
};

struct DecodingEntry
{
  std::string table_name;
  std::string description;
  std::bitset<64> instructions;
  std::bitset<64> subtables;
  int instruction_offset = 0;
  int subtable_offset = 0;
  int shift = 0;
  int length = 0;
  DecodingEntry(std::string name) : table_name(name) {}
};

static void CreateTable(std::vector<Instruction>& table, std::vector<DecodingEntry>& decoding_table,
                        DecodingEntry& entry, const std::vector<InputLine>& lines)
{
  entry.instruction_offset = table.size();
  entry.subtable_offset = decoding_table.size();
  std::vector<int> subtables;
  size_t start = 0;
  while (true)
  {
    if (start == lines.size())
    {
      std::cerr << "Error: subtable \"" << entry.table_name << "\" not found\n";
      std::exit(1);
    }
    if (std::holds_alternative<StartTableMarker>(lines[start]))
    {
      auto& marker = std::get<StartTableMarker>(lines[start]);
      if (marker.name == entry.table_name)
      {
        start += 1;
        entry.description = marker.description;
        entry.shift = marker.shift;
        entry.length = marker.length;
        break;
      }
    }
    start += 1;
  }
  size_t end;
  for (end = start; end < lines.size() && end - start < (1u << entry.length) &&
                    !std::holds_alternative<StartTableMarker>(lines[end]);
       end += 1)
  {
    entry.instructions <<= 1;
    entry.subtables <<= 1;
    if (std::holds_alternative<Instruction>(lines[end]))
    {
      table.push_back(std::get<Instruction>(lines[end]));
      entry.instructions |= 1;
    }
    else if (std::holds_alternative<SubTable>(lines[end]))
    {
      subtables.push_back(end);
      entry.subtables |= 1;
    }
  }
  if (end - start == (1u << entry.length))
  {
    for (size_t i = end; i < lines.size() && !std::holds_alternative<StartTableMarker>(lines[i]);
         i += 1)
    {
      if (!std::holds_alternative<Empty>(lines[i]))
      {
        std::cerr << "Error: subtable \"" << entry.table_name << "\" (" << entry.length
                  << "-bit field) is longer than " << (1 << entry.length) << "\n";
        std::exit(1);
      }
    }
  }
  entry.instructions <<= 64 + start - end;
  entry.subtables <<= 64 + start - end;
  int offset = entry.subtable_offset;
  for (auto& i : subtables)
  {
    // WARNING: may invalidate 'entry'
    decoding_table.push_back(DecodingEntry(std::get<SubTable>(lines[i]).name));
  }
  for (size_t i = 0; i < subtables.size(); i += 1)
  {
    CreateTable(table, decoding_table, decoding_table[offset + i], lines);
  }
}

static void DoOutput(const std::vector<Instruction>& table,
                     const std::vector<DecodingEntry>& decoding_table, const OutputOptions& options,
                     std::ostream& out)
{
  if (options.flags & OPID_RANGES)
  {
    for (auto& dec : decoding_table)
    {
      out << dec.table_name << " = " << dec.instruction_offset + 1 << ", // " << dec.description
          << "\n";
      out << dec.table_name << "_End = " << dec.instruction_offset + 1 + dec.instructions.count()
          << ",\n";
    }
    out << "End = " << table.size() + 1 << '\n';
  }
  if (options.flags & DECODING_TABLE)
  {
    for (auto& dec : decoding_table)
    {
      out << "{0x" << std::hex << std::setfill('0') << std::setw(16) << dec.instructions.to_ulong()
          << ", 0x" << std::setw(16) << dec.subtables.to_ulong() << std::dec
          << ", "
          // for binary literals: "{0b" << dec.instructions << ", 0b" << dec.subtables
          << dec.instruction_offset + 1 << ", " << dec.subtable_offset << ", " << dec.shift << ", "
          << dec.length << "},\n";
    }
  }
  if (options.flags & OPINFO_TABLE)
  {
    for (auto& inst : table)
    {
      out << "{\"" << inst.opname << "\", OpType::" << inst.type << ", " << inst.flags << "},\n";
    }
  }
  if (!options.columns.empty())
  {
    for (auto& inst : table)
    {
      if (options.columns.size() > 1)
      {
        out << "{";
      }
      for (auto& col : options.columns)
      {
        const std::string* val;
        if (inst.dispatch.size() > col.column && !inst.dispatch[col.column].empty())
        {
          val = &inst.dispatch[col.column];
        }
        else
        {
          val = &col.default_;
        }
        if (*val == "*")
        {
          out << col.prefix << inst.opname << ", \n";
        }
        else
        {
          out << *val << ", \n";
        }
      }
      if (options.columns.size() > 1)
      {
        out << "}";
      }
    }
  }
}

static void ReadTable(std::istream& in, std::vector<std::deque<std::string>>& rows)
{
  while (!in.eof())
  {
    std::string line;
    std::getline(in, line);
    std::deque<std::string>::size_type nextpos, pos = 0;
    std::deque<std::string> row;
    if (line.empty())
    {
      rows.push_back(row);
      continue;
    }
    while (true)
    {
      nextpos = line.find(SEPARATOR, pos);
      if (nextpos != std::string::npos)
      {
        row.push_back(line.substr(pos, nextpos - pos));
        pos = nextpos + 1;
      }
      else
      {
        row.push_back(line.substr(pos, std::string::npos));
        break;
      }
    }
    rows.push_back(row);
  }
}

int main(int argc, char** argv)
{
  std::vector<std::pair<std::ofstream, OutputOptions>> file_outputs;
  OutputOptions stdout_options;
  std::string inputfile;
  if (argc == 1)
  {
    const char* help_string =
        " [options]\n"
        "Options:\n"
        "-i <file>   read from this file, not stdin"
        "-o <file>   sets an output file for the preceding options. Trailing options\n"
        "            are applied for stdout.\n"
        "-r          generate OpID range definition\n"
        "-D          generate decoding table\n"
        "-I          generate opinfo table\n"
        "-0…9        Generate a custom table. Add the column specified by the digit.\n"
        "-p <prefix>  Set the prefix for the previously-defined column."
        "-d <default> Set the default value for the previously-defined column. (use * for the "
        "opname)\n";
    std::cout << "Usage: " << argv[0] << help_string;
    return 0;
  }
  for (int i = 1; i < argc; i += 1)
  {
    if (argv[i][0] != '-' || argv[i][1] == 0)
    {
      std::cerr << "Error: unrecognized command line argument \"" << argv[i]
                << "\".\n"
                   "Invoke without arguments for usage description\n";
      return 1;
    }
    for (int n = 1; argv[i][n]; n += 1)
    {
      bool skip = false;
      switch (argv[i][n])
      {
      case 'o':
        if (argv[i][n + 1] != 0 || i + 1 == argc)
        {
          std::cerr << "Error: the -o command line option expects an argument.\n"
                       "Invoke without options for usage description\n";
          return 1;
        }
        file_outputs.emplace_back(std::ofstream(argv[i + 1]), stdout_options);
        stdout_options = OutputOptions();
        i += 1;
        skip = true;
        break;
      case 'i':
        if (argv[i][n + 1] != 0 || i + 1 == argc)
        {
          std::cerr << "Error: the -i command line option expects an argument.\n"
                       "Invoke without options for usage description\n";
          return 1;
        }
        inputfile = argv[i + 1];
        i += 1;
        skip = true;
        break;
      case 'r':
        stdout_options.flags |= OPID_RANGES;
        break;
      case 'D':
        stdout_options.flags |= DECODING_TABLE;
        break;
      case 'I':
        stdout_options.flags |= OPINFO_TABLE;
        break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        stdout_options.columns.emplace_back(ColumnOptions{(unsigned int)argv[i][n] - '0', "", ""});
        break;
      case 'p':
        if (argv[i][n + 1] != 0 || i + 1 == argc)
        {
          std::cerr << "Error: the -p command line option expects an argument.\n"
                       "Invoke without options for usage description\n";
          return 1;
        }
        if (stdout_options.columns.empty())
        {
          std::cerr << "Error: no column was specified for -p to apply to.\n"
                       "Invoke without options for usage description\n";
        }
        stdout_options.columns.back().prefix = argv[i + 1];
        i += 1;
        skip = true;
        break;
      case 'd':
        if (argv[i][n + 1] != 0 || i + 1 == argc)
        {
          std::cerr << "Error: the -d command line option expects an argument.\n"
                       "Invoke without options for usage description\n";
          return 1;
        }
        if (stdout_options.columns.empty())
        {
          std::cerr << "Error: no column was specified for -d to apply to.\n"
                       "Invoke without options for usage description\n";
        }
        stdout_options.columns.back().default_ = argv[i + 1];
        i += 1;
        skip = true;
        break;
      default:
        std::cerr << "Error: unrecognized command line option -" << argv[i][n] << '\n';
        return 1;
      }
      if (skip)
      {
        break;
      }
    }
  }
  std::vector<std::deque<std::string>> rows;
  if (inputfile.empty())
  {
    ReadTable(std::cin, rows);
  }
  else
  {
    auto file = std::ifstream(inputfile);
    ReadTable(file, rows);
  }
  std::vector<InputLine> lines;
  for (auto& row : rows)
  {
    if (!row.empty())
    {
      std::string opname = row.front();
      row.pop_front();
      if (opname == "->")
      {
        if (row.size() < 1)
        {
          std::cerr << "Error: not enough cells for subtable reference in line "
                    << (lines.size() + 1) << '\n';
          return 1;
        }
        lines.push_back(SubTable{row[0]});
      }
      else if (opname == "===")
      {
        if (row.size() < 4)
        {
          std::cerr << "Error: not enough cells for subtable marker in line " << (lines.size() + 1)
                    << '\n';
          return 1;
        }
        int shift = std::stoi(row[1]);
        int len = std::stoi(row[2]);
        if (len > 6 || len < 1)
        {
          std::cerr << "Error: field for table \"" << row[0] << "\" is not 1–6 bits in line "
                    << (lines.size() + 1) << '\n';
          return 1;
        }
        if (shift > 32 - len || shift < 0)
        {
          std::cerr << "Error: field for table \"" << row[0]
                    << "\" has invalid shift value in line " << (lines.size() + 1) << '\n';
          return 1;
        }
        lines.push_back(StartTableMarker{row[0], shift, len, row[3]});
      }
      else if (opname.empty() || opname == "#")
      {
        // used for comments, treat as empty line
        lines.push_back(Empty());
      }
      else if (row.size() >= 2)
      {
        std::string type = row.front();
        row.pop_front();
        std::string flags = row.front();
        row.pop_front();
        lines.push_back(Instruction(opname, type, flags, row));
      }
      else
      {
        std::cerr << "Error: not enough cells for instruction description in line "
                  << (lines.size() + 1) << '\n';
        return 1;
      }
    }
    else
    {
      lines.push_back(Empty());
    }
  }
  std::vector<Instruction> table;
  std::vector<DecodingEntry> decoding_table;
  decoding_table.push_back(DecodingEntry("Primary"));
  CreateTable(table, decoding_table, decoding_table[0], lines);
  DoOutput(table, decoding_table, stdout_options, std::cout);
  for (auto& pair : file_outputs)
  {
    DoOutput(table, decoding_table, pair.second, pair.first);
  }
}
