"""Read local lookup data from the supported ARM64 binary; never execute it."""
import io
import struct


class TableError(ValueError):
    pass


class GameBinary:
    def __init__(self, data):
        try:
            from elftools.elf.elffile import ELFFile
            from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
        except ImportError as exc:
            raise TableError("Install requirements-setup.txt before preparing the APK.") from exc
        self.data = data
        self.elf = ELFFile(io.BytesIO(data))
        if self.elf['e_machine'] != 'EM_AARCH64' or not self.elf.little_endian:
            raise TableError("Expected a little-endian ARM64 libGame.so.")
        dynsym = self.elf.get_section_by_name('.dynsym')
        if dynsym is None:
            raise TableError("The game binary has no exported symbol table.")
        self.symbols = {s.name: s for s in dynsym.iter_symbols()}
        self.names = {s['st_value']: s.name for s in self.symbols.values() if s['st_value']}
        self.segments = [p for p in self.elf.iter_segments() if p['p_type'] == 'PT_LOAD']
        self.relocations = {}
        for section in self.elf.iter_sections():
            if section['sh_type'] not in ('SHT_RELA', 'SHT_REL'):
                continue
            symbols = self.elf.get_section(section['sh_link'])
            for rel in section.iter_relocations():
                if rel['r_info_sym'] and symbols:
                    self.relocations[rel['r_offset']] = symbols.get_symbol(rel['r_info_sym']).name
        self.decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
        self.decoder.detail = True

    def read(self, address, count):
        for seg in self.segments:
            offset = address - seg['p_vaddr']
            if 0 <= offset and offset + count <= seg['p_filesz']:
                start = seg['p_offset'] + offset
                return self.data[start:start + count]
        raise TableError(f"Address 0x{address:x} is not file-backed.")

    def symbol_data(self, name, count):
        symbol = self.symbols.get(name)
        if symbol is None or symbol['st_size'] != count:
            raise TableError(f"Unexpected layout for {name}; only Android 4.4.243 is supported.")
        return self.read(symbol['st_value'], count)

    def call_name(self, address):
        if address in self.names:
            return self.names[address]
        # Resolve an AArch64 PLT stub through its GOT relocation.
        page = None
        for ins in self.decoder.disasm(self.read(address, 16), address):
            if ins.mnemonic == 'adrp':
                page = ins.operands[1].imm
            elif ins.mnemonic == 'ldr' and page is not None:
                return self.relocations.get(page + ins.operands[1].mem.disp, '')
        return ''

    def constant_calls(self, name):
        """Track constant call arguments in the two straight-line table initializers."""
        from capstone.arm64 import ARM64_OP_IMM, ARM64_OP_REG
        symbol = self.symbols.get(name)
        if symbol is None:
            raise TableError(f"Missing initializer {name}.")
        regs = {'sp': 0x700000000000, 'x0': 0x600000000000}
        stack = {}

        def regname(ins, reg):
            name = ins.reg_name(reg)
            return 'x' + name[1:] if name.startswith('w') else name

        def value(ins, operand):
            if operand.type == ARM64_OP_IMM:
                return operand.imm
            if operand.type == ARM64_OP_REG:
                name = regname(ins, operand.reg)
                return 0 if name in ('xzr', 'wzr') else regs.get(name)
            return None

        def write(ins, operand, number):
            name = regname(ins, operand.reg)
            if ins.reg_name(operand.reg).startswith('w') and number is not None:
                number &= 0xFFFFFFFF
            regs[name] = number

        code = self.read(symbol['st_value'], symbol['st_size'])
        for ins in self.decoder.disasm(code, symbol['st_value']):
            op = ins.operands
            if ins.mnemonic in ('mov', 'adrp'):
                write(ins, op[0], value(ins, op[1]))
            elif ins.mnemonic in ('add', 'sub'):
                a, b = value(ins, op[1]), value(ins, op[2])
                if b is not None and op[2].shift.value:
                    b <<= op[2].shift.value
                write(ins, op[0], None if a is None or b is None else a + b if ins.mnemonic == 'add' else a - b)
            elif ins.mnemonic in ('str', 'strb', 'stur'):
                base = regs.get(regname(ins, op[1].mem.base))
                if base is not None:
                    stack[base + op[1].mem.disp] = value(ins, op[0])
            elif ins.mnemonic == 'bl':
                yield self.call_name(op[0].imm), dict(regs), dict(stack)
                for r in range(19):
                    regs.pop(f'x{r}', None)
            elif ins.mnemonic == 'ret':
                return
            else:
                for reg in ins.regs_access()[1]:
                    regs.pop(regname(ins, reg), None)

    def population(self):
        profiles = []
        for name, regs, stack in self.constant_calls('_ZN12cZoneManager17DefinePopProfilesEv'):
            if not name.startswith('_ZN17PopulationProfileC'):
                continue
            sp = regs.get('sp')
            values = [regs.get(f'x{i}') for i in range(1, 8)] + [stack.get(sp + i * 8) for i in range(5)]
            if any(v is None or not 0 <= v <= 255 for v in values):
                raise TableError("Could not resolve population profile arguments.")
            profiles.append(values)
        if not profiles:
            raise TableError("No population profiles found.")
        zones = []
        for name, regs, stack in self.constant_calls('_ZN12cZoneManager22SetupDefaultPopulationEv'):
            if not name.startswith('_ZNK8ZoneImpl17SetPopulationZone'):
                continue
            sp, address, profile = regs.get('sp'), regs.get('x1'), regs.get('x3')
            if address is None or profile is None:
                raise TableError("Could not resolve population zone arguments.")
            zone_name = self.read(address, 16).split(b'\0', 1)[0]
            index, remainder = divmod(profile - 0x600000000000, 20)
            values = [regs.get('x2'), index] + [regs.get(f'x{i}') for i in range(4, 8)] + [stack.get(sp), stack.get(sp + 8)]
            if not 1 <= len(zone_name) <= 8 or remainder or not 0 <= index < len(profiles) or any(v is None for v in values):
                raise TableError("Unsupported population zone layout.")
            zones.append(struct.pack('<8s8i', zone_name, *values))
        if not zones:
            raise TableError("No population zones found.")
        return (b'CTWZONE1' + struct.pack('<II', len(zones), len(profiles)) + b''.join(zones) +
                b''.join(struct.pack('<12i', *p) for p in profiles))

    def sound(self):
        data = b'CTWSND1\0'
        for name, size in [('gEventInfo', 156 * 16), ('gGears', 20 * 48),
                           ('gCarCollisionEventsLow', 12), ('gCarCollisionEventsMed', 12),
                           ('gCarCollisionEventsHigh', 12)]:
            data += self.symbol_data(name, size)
        # The rev-after-shift table has no exported name in this supported version.
        return data + self.read(0x480920, 16)


def extract_tables(binary):
    game = GameBinary(binary)
    return {'population_tables.bin': game.population(), 'sound_tables.bin': game.sound()}
