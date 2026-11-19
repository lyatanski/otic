// vim: ts=2:sw=2
#include "core.h"

// Debug information (-g), DWARF 5. The pass records as it goes: a row of the
// line table at each statement, the locals with the block they are in, the
// epilogues, each function when its body is done, each global, and a DIE for
// each type the first time something of that type is recorded (meta.die is
// its number, its place in dbg.offs). A form asks for the sections once the
// image is laid out and the addresses are known: one compilation unit for the
// whole program, one file of the line table for each unit of the pass. Names
// are inline strings, so there are no string sections

enum { // tags
  DW_TAG_array_type = 0x01, DW_TAG_formal_parameter = 0x05, DW_TAG_lexical_block = 0x0b,
  DW_TAG_member = 0x0d, DW_TAG_pointer_type = 0x0f, DW_TAG_compile_unit = 0x11,
  DW_TAG_structure_type = 0x13, DW_TAG_subroutine_type = 0x15, DW_TAG_subrange_type = 0x21,
  DW_TAG_base_type = 0x24, DW_TAG_subprogram = 0x2e, DW_TAG_variable = 0x34,
};
enum { // attributes
  DW_AT_location = 0x02, DW_AT_name = 0x03, DW_AT_byte_size = 0x0b, DW_AT_bit_size = 0x0d,
  DW_AT_stmt_list = 0x10, DW_AT_low_pc = 0x11, DW_AT_high_pc = 0x12, DW_AT_language = 0x13,
  DW_AT_comp_dir = 0x1b, DW_AT_producer = 0x25, DW_AT_prototyped = 0x27, DW_AT_count = 0x37,
  DW_AT_data_member_location = 0x38, DW_AT_decl_file = 0x3a, DW_AT_decl_line = 0x3b,
  DW_AT_encoding = 0x3e, DW_AT_external = 0x3f, DW_AT_frame_base = 0x40, DW_AT_type = 0x49,
  DW_AT_data_bit_offset = 0x6b,
};
enum { // forms
  DW_FORM_addr = 0x01, DW_FORM_data8 = 0x07, DW_FORM_string = 0x08, DW_FORM_data1 = 0x0b,
  DW_FORM_udata = 0x0f, DW_FORM_ref4 = 0x13, DW_FORM_sec_offset = 0x17, DW_FORM_exprloc = 0x18,
  DW_FORM_flag_present = 0x19,
};
enum { DW_ATE_boolean = 2, DW_ATE_float = 4, DW_ATE_signed = 5, DW_ATE_unsigned = 7, DW_ATE_unsigned_char = 8 };
enum { DW_OP_addr = 0x03, DW_OP_fbreg = 0x91, DW_OP_call_frame_cfa = 0x9c };
enum { DW_LANG_C99 = 0x0c };
enum { DW_LNS_copy = 1, DW_LNS_advance_pc, DW_LNS_advance_line, DW_LNS_set_file };
enum { // call frames
  DW_CFA_advance_loc = 0x40, DW_CFA_offset = 0x80, DW_CFA_advance_loc1 = 0x02,
  DW_CFA_advance_loc2 = 0x03, DW_CFA_advance_loc4 = 0x04, DW_CFA_remember_state = 0x0a,
  DW_CFA_restore_state = 0x0b, DW_CFA_def_cfa = 0x0c, DW_CFA_def_cfa_register = 0x0d,
  DW_CFA_def_cfa_offset = 0x0e,
};

// the abbreviations, numbered from 1 in this order. A function is a_func plus
// 1 when it is external, 2 when it has a type and 4 when it has nothing inside,
// a global a_glob plus 1 when it is external. An overload is named as written,
// not by its symbol (no DW_AT_linkage_name): gdb takes that for a C function's
// name, so that break name would miss the overloads
enum {
  a_unit = 1, a_base, a_ptr, a_ptrv, a_struct, a_anon, a_memb, a_bitf, a_array, a_range, a_proc, a_procv,
  a_func, a_glob = a_func + 8, a_local = a_glob + 2, a_parm, a_block,
};
typedef struct { byte tag, kids, at[10][2]; } dwabb; // the attributes and their forms, 0 ended
#define A_NAME {DW_AT_name, DW_FORM_string}
#define A_TYPE {DW_AT_type, DW_FORM_ref4}
#define A_PLACE {DW_AT_decl_file, DW_FORM_udata}, {DW_AT_decl_line, DW_FORM_udata}
#define A_EXTERN {DW_AT_external, DW_FORM_flag_present}
#define A_PROTO {DW_AT_prototyped, DW_FORM_flag_present}
#define A_CODE {DW_AT_low_pc, DW_FORM_addr}, {DW_AT_high_pc, DW_FORM_data8}
#define A_FRAME {DW_AT_frame_base, DW_FORM_exprloc}
#define A_WHERE {DW_AT_location, DW_FORM_exprloc}
static const dwabb abbs[] = {
  {DW_TAG_compile_unit, 1, {{DW_AT_producer, DW_FORM_string}, {DW_AT_language, DW_FORM_data1}, A_NAME,
    {DW_AT_comp_dir, DW_FORM_string}, {DW_AT_stmt_list, DW_FORM_sec_offset}, A_CODE}},
  {DW_TAG_base_type, 0, {A_NAME, {DW_AT_byte_size, DW_FORM_data1}, {DW_AT_encoding, DW_FORM_data1}}},
  {DW_TAG_pointer_type, 0, {{DW_AT_byte_size, DW_FORM_data1}, A_TYPE}},
  {DW_TAG_pointer_type, 0, {{DW_AT_byte_size, DW_FORM_data1}}},
  {DW_TAG_structure_type, 1, {A_NAME, {DW_AT_byte_size, DW_FORM_udata}}},
  {DW_TAG_structure_type, 1, {{DW_AT_byte_size, DW_FORM_udata}}},
  {DW_TAG_member, 0, {A_NAME, A_TYPE, {DW_AT_data_member_location, DW_FORM_udata}}},
  {DW_TAG_member, 0, {A_NAME, A_TYPE, {DW_AT_bit_size, DW_FORM_data1}, {DW_AT_data_bit_offset, DW_FORM_udata}}},
  {DW_TAG_array_type, 1, {A_TYPE}},
  {DW_TAG_subrange_type, 0, {{DW_AT_count, DW_FORM_udata}}},
  {DW_TAG_subroutine_type, 0, {A_TYPE}},
  {DW_TAG_subroutine_type, 0, {{0}}},
  {DW_TAG_subprogram, 1, {A_NAME, A_PLACE, A_PROTO, A_CODE, A_FRAME}},
  {DW_TAG_subprogram, 1, {A_EXTERN, A_NAME, A_PLACE, A_PROTO, A_CODE, A_FRAME}},
  {DW_TAG_subprogram, 1, {A_NAME, A_PLACE, A_PROTO, A_TYPE, A_CODE, A_FRAME}},
  {DW_TAG_subprogram, 1, {A_EXTERN, A_NAME, A_PLACE, A_PROTO, A_TYPE, A_CODE, A_FRAME}},
  {DW_TAG_subprogram, 0, {A_NAME, A_PLACE, A_PROTO, A_CODE, A_FRAME}},
  {DW_TAG_subprogram, 0, {A_EXTERN, A_NAME, A_PLACE, A_PROTO, A_CODE, A_FRAME}},
  {DW_TAG_subprogram, 0, {A_NAME, A_PLACE, A_PROTO, A_TYPE, A_CODE, A_FRAME}},
  {DW_TAG_subprogram, 0, {A_EXTERN, A_NAME, A_PLACE, A_PROTO, A_TYPE, A_CODE, A_FRAME}},
  {DW_TAG_variable, 0, {A_NAME, A_PLACE, A_TYPE, A_WHERE}},
  {DW_TAG_variable, 0, {A_EXTERN, A_NAME, A_PLACE, A_TYPE, A_WHERE}},
  {DW_TAG_variable, 0, {A_NAME, A_PLACE, A_TYPE, A_WHERE}},
  {DW_TAG_formal_parameter, 0, {A_NAME, A_PLACE, A_TYPE, A_WHERE}},
  {DW_TAG_lexical_block, 1, {A_CODE}},
};
#undef A_NAME
#undef A_TYPE
#undef A_PLACE
#undef A_EXTERN
#undef A_PROTO
#undef A_CODE
#undef A_FRAME
#undef A_WHERE

// ---- bytes

// v as n bytes, little endian
static void  dw_put(const mode* self, blob* b, u64i v, u32i n) {
  while(n--) {
    b->p = swell(self, b->p, b->n, &b->c, 1, 256);
    b->p[b->n++] = (byte)v;
    v >>= 8;
  }
}

static void  dw_uleb(const mode* self, blob* b, u64i v) {
  do dw_put(self, b, (v & 0x7f) | (v > 0x7f? 0x80: 0), 1); while((v >>= 7));
}

static void  dw_sleb(const mode* self, blob* b, s64i v) {
  for(bool more = 1; more; ) {
    byte c = v & 0x7f;
    v >>= 7; // arithmetic
    more = !((v == 0 && !(c & 0x40)) || (v == -1 && (c & 0x40)));
    dw_put(self, b, c | (more? 0x80: 0), 1);
  }
}

// the n bytes of s and a NUL
static void  dw_text(const mode* self, blob* b, const char* s, u64i n) {
  for(u64i i = 0; i < n; i++) dw_put(self, b, (byte)s[i], 1);
  dw_put(self, b, 0, 1);
}

static void  dw_patch(blob* b, u32i at, u64i v, u32i n) {
  for(u32i i = 0; i < n; i++, v >>= 8) b->p[at + i] = (byte)v;
}

// ---- types

// whether the spelling of name is s
static bool  dw_isit(const item* name, const char* s) {
  return name && name->size == slen(s) && alike(name->name, s, name->size);
}

// the number of type t in the debug information, made with its DIE when new, 0
// for void. A DIE goes after those of the types it is made of, and refers to
// them (and to itself, through a pointer) by number: dwarf_pack patches the
// places
static u32i  dw_tyid(mode* self, meta* t) {
  if(!t || (t->kind == base && !t->size)) return 0;
  if(t->kind == bitf) return dw_tyid(self, t->type);
  if(t->die) return t->die;
  dbg* d = self->debug? self->dbg: NULL;
  if(!d || (t->kind != base && t->kind != refs && t->kind != many && t->kind != list && t->kind != proc)) return 0;
  d->offs = swell(self, d->offs, d->ids + 1, &d->coff, sizeof(u32i), 64);
  u32i id = t->die = ++d->ids;
  blob* b = &d->tys;
  switch(t->kind) {
    case base: {
      u32i enc = dw_isit(t->name, "bool")? DW_ATE_boolean: dw_isit(t->name, "real")? DW_ATE_float:
                 dw_isit(t->name, "char")? DW_ATE_unsigned_char: dw_isit(t->name, "int") || dw_isit(t->name, "long")?
                 DW_ATE_signed: DW_ATE_unsigned;
      d->offs[id] = b->n;
      dw_uleb(self, b, a_base);
      dw_text(self, b, t->name->name, t->name->size);
      dw_put(self, b, t->size, 1);
      dw_put(self, b, enc, 1);
      break;
    }
    case refs: case many: case proc: {
      u32i to = dw_tyid(self, t->type);
      d->offs[id] = b->n;
      if(t->kind == proc) dw_uleb(self, b, to? a_proc: a_procv);
      else if(t->kind == many) dw_uleb(self, b, a_array);
      else dw_uleb(self, b, to? a_ptr: a_ptrv), dw_put(self, b, 8, 1);
      if(to) {
        d->fixs = swell(self, d->fixs, d->nfix, &d->cfix, sizeof(dwfix), 64);
        d->fixs[d->nfix++] = (dwfix){b->n, to};
        dw_put(self, b, 0, 4);
      }
      if(t->kind == many) {
        u32i size = t->type->size;
        dw_uleb(self, b, a_range);
        dw_uleb(self, b, size? t->size / size: 0);
        dw_put(self, b, 0, 1);
      }
      break;
    }
    default: { // a struct
      for(const meta* m = t->type; m; m = m->hide) dw_tyid(self, m->type);
      d->offs[id] = b->n;
      dw_uleb(self, b, t->name? a_struct: a_anon);
      if(t->name) dw_text(self, b, t->name->name, t->name->size);
      dw_uleb(self, b, t->size);
      for(const meta* m = t->type; m; m = m->hide) {
        const meta* x = m->type->kind == bitf? m->type->type: m->type;
        if(!m->name || !x->die) continue;
        dw_uleb(self, b, m->type->kind == bitf? a_bitf: a_memb);
        dw_text(self, b, m->name->name, m->name->size);
        d->fixs = swell(self, d->fixs, d->nfix, &d->cfix, sizeof(dwfix), 64);
        d->fixs[d->nfix++] = (dwfix){b->n, x->die};
        dw_put(self, b, 0, 4);
        if(m->type->kind == bitf) {
          dw_put(self, b, m->type->longs, 1);
          dw_uleb(self, b, (u64i)m->slot * 8 + m->type->slot);
        }
        else dw_uleb(self, b, (u32i)m->slot);
      }
      dw_put(self, b, 0, 1);
    }
  }
  return id;
}

// ---- what the pass records

static dbg*  dw_note(const mode* self) { return self->debug? self->dbg: NULL; }
static u32i  dw_here(const mode* self) { return (u32i)(self->text->body - self->text->head); }

// a row of the line table at text offset at: one row for an address, and none
// for a line that goes on
static void  dw_row(mode* self, u32i at, u32i line, bool force) {
  dbg* d = dw_note(self);
  if(!d) return;
  dwrow* last = d->nrow? d->rows + d->nrow - 1: NULL;
  if(last && last->at == at) { last->unit = self->unit, last->line = line; return; }
  if(last && !force && last->unit == self->unit && last->line == line) return;
  d->rows = swell(self, d->rows, d->nrow, &d->crow, sizeof(dwrow), 256);
  d->rows[d->nrow++] = (dwrow){at, self->unit, line};
}

void  dwarf_line(mode* self) {
  if(dw_note(self)) dw_row(self, dw_here(self), (u32i)home(self)->line, 0);
}

// text rewound to mark: what was recorded for the code past it goes. A row at
// mark itself is of the statement that goes on from there
void  dwarf_trim(mode* self, const byte* mark) {
  dbg* d = dw_note(self);
  if(!d) return;
  u32i at = (u32i)(mark - self->text->head);
  while(d->nrow && d->rows[d->nrow - 1].at > at) d->nrow--;
  while(d->nend && d->ends[d->nend - 1].lo >= at) d->nend--;
}

// a function begins at the entry of its code, called at line
void  dwarf_open(mode* self, u64i line) {
  dbg* d = dw_note(self);
  if(!d) return;
  d->vars0 = d->nvar, d->blks0 = d->nblk, d->ends0 = d->nend, d->deep = 0;
  dw_row(self, dw_here(self), (u32i)line, 1);
}

// a block begins: the outermost one is the function itself
void  dwarf_enter(mode* self) {
  dbg* d = dw_note(self);
  if(!d) return;
  d->open = swell(self, d->open, d->deep, &d->copen, sizeof(u32i), 16);
  if(!d->deep) { d->open[d->deep++] = 0; return; }
  d->blks = swell(self, d->blks, d->nblk, &d->cblk, sizeof(dwblk), 64);
  d->blks[d->nblk++] = (dwblk){dw_here(self), dw_here(self), d->open[d->deep - 1]};
  d->open[d->deep++] = d->nblk; // the block, plus 1
}

void  dwarf_leave(mode* self) {
  dbg* d = dw_note(self);
  if(!d || !d->deep) return;
  u32i b = d->open[--d->deep];
  if(b) d->blks[b - 1].hi = dw_here(self);
}

// a local or parameter, at frame slot from rbp (x29), in the innermost block
void  dwarf_var(mode* self, item* name, meta* type, s32i slot, bool parm, u64i line) {
  dbg* d = dw_note(self);
  if(!d || slot >= 0) return; // a register has no place the debugger reads
  u32i t = dw_tyid(self, type);
  if(!t) return;
  d->vars = swell(self, d->vars, d->nvar, &d->cvar, sizeof(dwvar), 64);
  d->vars[d->nvar++] = (dwvar){name, t, self->unit, (u32i)line, d->deep? d->open[d->deep - 1]: 0, slot, parm};
}

// an epilogue from there to the end of text
void  dwarf_done(mode* self, const byte* from) {
  dbg* d = dw_note(self);
  if(!d) return;
  d->ends = swell(self, d->ends, d->nend, &d->cend, sizeof(dwend), 16);
  d->ends[d->nend++] = (dwend){(u32i)(from - self->text->head), dw_here(self)};
}

// the function, its body done, from its entry at lo
void  dwarf_func(mode* self, const meta* func, u64i line, const byte* lo) {
  dbg* d = dw_note(self);
  if(!d) return;
  d->funs = swell(self, d->funs, d->nfun, &d->cfun, sizeof(dwfun), 64);
  d->funs[d->nfun++] = (dwfun){func->name, func, dw_tyid(self, func->type), self->unit, (u32i)line,
    (u32i)(lo - self->text->head), dw_here(self), func->own,
    d->vars0, d->nvar - d->vars0, d->blks0, d->nblk - d->blks0, d->ends0, d->nend - d->ends0};
}

void  dwarf_glob(mode* self, item* name, meta* type, u32i slot, bool own, u64i line) {
  dbg* d = dw_note(self);
  u32i t = d? dw_tyid(self, type): 0;
  if(!t) return;
  d->glbs = swell(self, d->glbs, d->nglb, &d->cglb, sizeof(dwglb), 64);
  d->glbs[d->nglb++] = (dwglb){name, t, self->unit, (u32i)line, slot, type->size, own};
}

// ---- the sections

// the DIEs of a function's block (0: the function), its locals and the blocks
// inside, the ones with no local at all left out
static u32i  dw_count(const dbg* d, const dwfun* f, u32i block) {
  u32i n = 0;
  for(u32i i = f->vars; i < f->vars + f->nvars; i++) n += d->vars[i].block == block;
  for(u32i i = f->blks; i < f->blks + f->nblks; i++) if(d->blks[i].up == block) n += dw_count(d, f, i + 1);
  return n;
}

static void  dw_fill(mode* self, const dwfun* f, u32i block, blob* b, u32i shift, u64i text, const u32i* offs) {
  const dbg* d = self->dbg;
  for(u32i i = f->vars; i < f->vars + f->nvars; i++) {
    const dwvar* v = d->vars + i;
    if(v->block != block) continue;
    byte  loc[16];
    blob  e = {loc, 0, sizeof loc};
    dw_sleb(self, &e, v->slot - 16); // from the CFA: the saved frame pointer and the return address are above rbp
    dw_uleb(self, b, v->parm? a_parm: a_local);
    dw_text(self, b, v->name->name, v->name->size);
    dw_uleb(self, b, v->unit);
    dw_uleb(self, b, v->line);
    dw_put(self, b, shift + offs[v->type], 4);
    dw_uleb(self, b, e.n + 1);
    dw_put(self, b, DW_OP_fbreg, 1);
    for(u32i k = 0; k < e.n; k++) dw_put(self, b, loc[k], 1);
  }
  for(u32i i = f->blks; i < f->blks + f->nblks; i++) {
    const dwblk* k = d->blks + i;
    if(k->up != block || !dw_count(d, f, i + 1)) continue;
    dw_uleb(self, b, a_block);
    dw_put(self, b, text + k->lo, 8);
    dw_put(self, b, k->hi - k->lo, 8);
    dw_fill(self, f, i + 1, b, shift, text, offs);
    dw_put(self, b, 0, 1);
  }
}

// .eh_frame: one CIE for the fixed shape of every frame, and an FDE for each
// function: the frame pointer is saved and set up by the first two
// instructions (push rbp; mov rbp, rsp, or stp x29, x30, [sp, #-16]!; mov x29,
// sp), and an epilogue takes it down again
static void  dw_frames(mode* self, blob* b, u64i text) {
  const dbg* d = self->dbg;
  const bool arm = self->arch.cpu == cpu_arm8;
  const u32i pre = arm? 4: 1, set = arm? 4: 3; // bytes of the push, and of the move
  const u32i down = arm? 8: 1; // bytes of an epilogue before the stack is the caller's
  const u32i fp = arm? 29: 6, sp = arm? 31: 7, ra = arm? 30: 16;
  u32i cie = b->n;
  dw_put(self, b, 0, 4); // length
  dw_put(self, b, 0, 4); // the id of a CIE
  dw_put(self, b, 1, 1);
  dw_text(self, b, "zR", 2);
  dw_uleb(self, b, 1);
  dw_sleb(self, b, -8);
  dw_put(self, b, ra, 1);
  dw_uleb(self, b, 1);
  dw_put(self, b, 0, 1); // addresses as they are
  if(arm) { dw_put(self, b, DW_CFA_def_cfa, 1); dw_uleb(self, b, sp); dw_uleb(self, b, 0); }
  else {
    dw_put(self, b, DW_CFA_def_cfa, 1); dw_uleb(self, b, sp); dw_uleb(self, b, 8);
    dw_put(self, b, DW_CFA_offset | ra, 1); dw_uleb(self, b, 1);
  }
  while(b->n % 8) dw_put(self, b, 0, 1);
  dw_patch(b, cie, b->n - cie - 4, 4);

  for(u32i i = 0; i < d->nfun; i++) {
    const dwfun* f = d->funs + i;
    u32i at = b->n, pos = 0; // the code the instructions have reached, from the entry
#define ADVANCE(to) do { u32i n = (to) - pos; pos = (to); \
    if(n < 64) dw_put(self, b, DW_CFA_advance_loc | n, 1); \
    else if(n < 256) { dw_put(self, b, DW_CFA_advance_loc1, 1); dw_put(self, b, n, 1); } \
    else if(n < 65536) { dw_put(self, b, DW_CFA_advance_loc2, 1); dw_put(self, b, n, 2); } \
    else { dw_put(self, b, DW_CFA_advance_loc4, 1); dw_put(self, b, n, 4); } } while(0)
    dw_put(self, b, 0, 4);
    dw_put(self, b, at - cie + 4, 4); // back to the CIE
    dw_put(self, b, text + f->lo, 8);
    dw_put(self, b, f->hi - f->lo, 8);
    dw_uleb(self, b, 0);
    ADVANCE(pre);
    dw_put(self, b, DW_CFA_def_cfa_offset, 1); dw_uleb(self, b, 16);
    dw_put(self, b, DW_CFA_offset | fp, 1); dw_uleb(self, b, 2);
    if(arm) { dw_put(self, b, DW_CFA_offset | ra, 1); dw_uleb(self, b, 1); }
    ADVANCE(pre + set);
    dw_put(self, b, DW_CFA_def_cfa_register, 1); dw_uleb(self, b, fp);
    for(u32i k = f->ends; k < f->ends + f->nends; k++) {
      const dwend* e = d->ends + k;
      if(e->lo + down < f->lo + pos || e->hi > f->hi) continue;
      ADVANCE(e->lo + down - f->lo);
      dw_put(self, b, DW_CFA_remember_state, 1);
      dw_put(self, b, DW_CFA_def_cfa, 1); dw_uleb(self, b, sp); dw_uleb(self, b, arm? 0: 8);
      ADVANCE(e->hi - f->lo);
      dw_put(self, b, DW_CFA_restore_state, 1);
    }
#undef ADVANCE
    while(b->n % 8) dw_put(self, b, 0, 1);
    dw_patch(b, at, b->n - at - 4, 4);
  }
  dw_put(self, b, 0, 4); // the end
}

// .debug_line: the files are the units of the pass, a row in one sequence for each
// statement
static void  dw_lines(mode* self, blob* b, u64i text, u64i code) {
  const dbg* d = self->dbg;
  static const byte lens[] = {0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};
  dw_put(self, b, 0, 4); // length
  dw_put(self, b, 5, 2);
  dw_put(self, b, 8, 1); // addresses are 8 bytes
  dw_put(self, b, 0, 1);
  u32i hdr = b->n;
  dw_put(self, b, 0, 4); // header length
  dw_put(self, b, 1, 1);
  dw_put(self, b, 1, 1);
  dw_put(self, b, 1, 1);
  dw_put(self, b, (byte)-5, 1);
  dw_put(self, b, 14, 1);
  dw_put(self, b, 13, 1);
  for(u32i i = 0; i < sizeof lens; i++) dw_put(self, b, lens[i], 1);
  dw_put(self, b, 1, 1); dw_uleb(self, b, 1); dw_uleb(self, b, DW_FORM_string); // directories: path
  dw_uleb(self, b, 1);
  const char* cwd = self->cwd? self->cwd: ".";
  dw_text(self, b, cwd, slen(cwd));
  dw_put(self, b, 2, 1); dw_uleb(self, b, 1); dw_uleb(self, b, DW_FORM_string); // files: path, directory
  dw_uleb(self, b, 2); dw_uleb(self, b, DW_FORM_udata);
  dw_uleb(self, b, self->unitn);
  for(u32i i = 0; i < self->unitn; i++) {
    const char* path = self->units[i].path? self->units[i].path: "";
    dw_text(self, b, path, slen(path));
    dw_uleb(self, b, 0);
  }
  dw_patch(b, hdr, b->n - hdr - 4, 4);

  u64i  at = 0;
  u32i  file = 1, line = 1;
  dw_put(self, b, 0, 1); dw_uleb(self, b, 9); dw_put(self, b, 2, 1); dw_put(self, b, text, 8); // set_address
  for(u32i i = 0; i < d->nrow; i++) {
    const dwrow* r = d->rows + i;
    if(r->unit != file) { dw_put(self, b, DW_LNS_set_file, 1); dw_uleb(self, b, r->unit); file = r->unit; }
    if(r->line != line) { dw_put(self, b, DW_LNS_advance_line, 1); dw_sleb(self, b, (s64i)r->line - line); line = r->line; }
    if(r->at != at) { dw_put(self, b, DW_LNS_advance_pc, 1); dw_uleb(self, b, r->at - at); at = r->at; }
    dw_put(self, b, DW_LNS_copy, 1);
  }
  dw_put(self, b, DW_LNS_advance_pc, 1); dw_uleb(self, b, code - at);
  dw_put(self, b, 0, 1); dw_uleb(self, b, 1); dw_put(self, b, 1, 1); // end_sequence
  dw_patch(b, 0, b->n - 4, 4);
}

// the sections of the image whose text, data and bss are at those addresses
void  dwarf_pack(mode* self, u64i text, u64i data, u64i bss, dwarf* out) {
  dbg* d = self->dbg;
  const u64i code = self->text->body - self->text->head;
  *out = (dwarf){0};

  for(u32i i = 0; i < sizeof abbs / sizeof *abbs; i++) {
    dw_uleb(self, &out->abbr, i + 1);
    dw_uleb(self, &out->abbr, abbs[i].tag);
    dw_put(self, &out->abbr, abbs[i].kids, 1);
    for(const byte(*a)[2] = abbs[i].at; (*a)[0]; a++) { dw_uleb(self, &out->abbr, (*a)[0]); dw_uleb(self, &out->abbr, (*a)[1]); }
    dw_put(self, &out->abbr, 0, 2);
  }
  dw_put(self, &out->abbr, 0, 1);

  blob* b = &out->info;
  dw_put(self, b, 0, 4); // length
  dw_put(self, b, 5, 2);
  dw_put(self, b, 1, 1); // a unit with its own code
  dw_put(self, b, 8, 1);
  dw_put(self, b, 0, 4); // its abbreviations
  dw_uleb(self, b, a_unit);
  dw_text(self, b, "otic", 4);
  dw_put(self, b, DW_LANG_C99, 1);
  dw_text(self, b, self->name, slen(self->name));
  dw_text(self, b, self->cwd? self->cwd: ".", slen(self->cwd? self->cwd: "."));
  dw_put(self, b, 0, 4); // the line table
  dw_put(self, b, text, 8);
  dw_put(self, b, code, 8);
  const u32i shift = b->n;
  for(u32i i = 0; i < d->tys.n; i++) dw_put(self, b, d->tys.p[i], 1);
  for(u32i i = 0; i < d->nfix; i++) dw_patch(b, shift + d->fixs[i].at, shift + d->offs[d->fixs[i].id], 4);

  for(u32i i = 0; i < d->nglb; i++) {
    const dwglb* g = d->glbs + i;
    dw_uleb(self, b, a_glob + !g->own);
    dw_text(self, b, g->name->name, g->name->size);
    dw_uleb(self, b, g->unit);
    dw_uleb(self, b, g->line);
    dw_put(self, b, shift + d->offs[g->type], 4);
    dw_uleb(self, b, 9);
    dw_put(self, b, DW_OP_addr, 1);
    dw_put(self, b, g->slot & ZERO? bss + (g->slot & ~ZERO): data + g->slot, 8);
  }
  for(u32i i = 0; i < d->nfun; i++) {
    const dwfun* f = d->funs + i;
    bool leaf = !dw_count(d, f, 0);
    dw_uleb(self, b, a_func + !f->own + 2 * !!f->type + 4 * leaf);
    dw_text(self, b, f->name->name, f->name->size);
    dw_uleb(self, b, f->unit);
    dw_uleb(self, b, f->line);
    if(f->type) dw_put(self, b, shift + d->offs[f->type], 4);
    dw_put(self, b, text + f->lo, 8);
    dw_put(self, b, f->hi - f->lo, 8);
    dw_uleb(self, b, 1);
    dw_put(self, b, DW_OP_call_frame_cfa, 1);
    if(leaf) continue;
    dw_fill(self, f, 0, b, shift, text, d->offs);
    dw_put(self, b, 0, 1);
  }
  dw_put(self, b, 0, 1); // the end of the unit's children
  dw_patch(b, 0, b->n - 4, 4);

  // .debug_aranges: the code of the unit, for a debugger that looks an address up
  blob* r = &out->ranges;
  dw_put(self, r, 0, 4);
  dw_put(self, r, 2, 2);
  dw_put(self, r, 0, 4); // the unit
  dw_put(self, r, 8, 1);
  dw_put(self, r, 0, 1);
  dw_put(self, r, 0, 4); // the tuples start at a multiple of 16
  dw_put(self, r, text, 8);
  dw_put(self, r, code, 8);
  dw_put(self, r, 0, 16);
  dw_patch(r, 0, r->n - 4, 4);

  dw_lines(self, &out->line, text, code);
  dw_frames(self, &out->frame, text);
}
