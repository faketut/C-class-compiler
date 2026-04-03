# ARM64 Reference Sheet

### 3-Register Format

**Machine-code encoding (big-endian)**

| Instruction | Assembly Syntax | Behaviour | Opcode (bit pattern) |
|-------------|-----------------|-----------|----------------------|
| Add | `add xd, xn, xm` | xd = xn + xm | `10001011 001 mmmmm 011000 nn nnn ddddd` |
| Subtract | `sub xd, xn, xm` | xd = xn − xm | `11001011 001 mmmmm 011000 nn nnn ddddd` |
| Multiply | `mul xd, xn, xm` | xd = (xn · xm) mod 2⁶⁴ | `10011011 000 mmmmm 011111 nn nnn ddddd` |
| Signed High Mul | `smulh xd, xn, xm` | xd = (xn · xm) / 2⁶⁴ | `10011011 010 mmmmm 011111 nn nnn ddddd` |
| Unsigned High Mul | `umulh xd, xn, xm` | xd = (xn · xm) / 2⁶⁴ | `10011011 110 mmmmm 011111 nn nnn ddddd` |
| Signed Divide | `sdiv xd, xn, xm` | xd = xn / xm | `10011010 110 mmmmm 000011 nn nnn ddddd` |
| Unsigned Divide | `udiv xd, xn, xm` | xd = xn / xm | `10011010 110 mmmmm 000010 nn nnn ddddd` |
| Compare | `cmp xn, xm` | compare xn and xm | `11101011 001 mmmmm 011000 nn nnn 11111` |
| Branch Register | `br xn` | NPC = xn | `11010110 000 11111 000000 nn nnn 00000` |
| Branch & Link Reg | `blr xn` | NPC = xn; x30 = PC + 4 | `11010110 001 11111 000000 nn nnn 00000` |

---

### 2-Register Format

| Instruction | Assembly Syntax | Behaviour | Opcode |
|-------------|-----------------|-----------|--------|
| Load Register | `ldur xd, [xn, i]` | xd = MEM[xn + i] | `11111000 010 iiiii iiii 00 nn nnn ddddd` |
| Store Register | `stur xd, [xn, i]` | MEM[xn + i] = xd | `11111000 000 iiiii iiii 00 nn nnn ddddd` |

---

### 1-Register Format

| Instruction | Assembly Syntax | Behaviour | Opcode |
|-------------|-----------------|-----------|--------|
| PC-Relative Load | `ldr xd, i` | xd = MEM[PC + i · 4] | `01011000 iiiiiiii iiiiiiii iii ddddd` |

---

### Branching Format

| Instruction | Assembly Syntax | Behaviour | Opcode |
|-------------|-----------------|-----------|--------|
| Branch | `b i` | NPC = PC + i · 4 | `000101 ii iiiiiiii iiiiiiii iiiiiiii` |
| Conditional Branch | `b.cond i` | if (cond) NPC = PC + i · 4 | `01010100 iiiiiiii iiiiiiii iiiccccc` |

---

### Condition Codes

| Condition | Bits |
|-----------|------|
| eq | 00000 |
| ne | 00001 |
| hs (unsigned ≥) | 00010 |
| lo (unsigned <) | 00011 |
| hi (unsigned >) | 01000 |
| ls (unsigned ≤) | 01001 |
| ge (signed ≥) | 01010 |
| lt (signed <) | 01011 |
| gt (signed >) | 01100 |
| le (signed ≤) | 01101 |
