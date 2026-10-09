use ovel_cst::{CstType, TypePrim};

// ─── Symbol kinds ───────────────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SymbolKind {
    Class, Method, Ivar, Property,
    Protocol, Variable, Type, Selector, Function,
}

// ─── NpType (resolved type) ─────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct NpType {
    pub prim: TypePrim,
    pub is_pointer: bool,
    pub is_const: bool,
    pub is_block: bool,
    pub is_array: bool,
    pub is_struct: bool,
    pub array_size: i32,
    pub subtype: Option<Box<NpType>>,
    pub block_params: Option<Box<NpType>>,
    pub next: Option<Box<NpType>>,
    pub type_args: Vec<NpType>,
    pub name: Option<String>,
    pub class_ref: Option<String>,
    pub protocol_ref: Option<String>,
    pub protocol_refs: Vec<String>,
    pub protocol_names: Vec<String>,
}

impl NpType {
    pub fn new(prim: TypePrim) -> Self {
        NpType {
            prim, is_pointer: false, is_const: false, is_block: false,
            is_array: false, is_struct: false, array_size: 0,
            subtype: None, block_params: None, next: None,
            type_args: Vec::new(), name: None,
            class_ref: None, protocol_ref: None,
            protocol_refs: Vec::new(), protocol_names: Vec::new(),
        }
    }

    pub fn from_cst(t: &CstType) -> Self {
        NpType {
            prim: t.prim,
            is_pointer: t.is_pointer,
            is_const: t.is_const,
            is_block: t.is_block,
            is_array: t.is_array,
            is_struct: t.is_struct,
            array_size: t.array_size,
            subtype: t.subtype.as_ref().map(|s| Box::new(NpType::from_cst(s))),
            block_params: t.block_params.as_ref().map(|b| Box::new(NpType::from_cst(b))),
            next: t.next.as_ref().map(|n| Box::new(NpType::from_cst(n))),
            type_args: t.type_args.iter().map(|a| NpType::from_cst(a)).collect(),
            name: t.name.clone(),
            class_ref: None,
            protocol_ref: None,
            protocol_refs: t.protocols.clone(),
            protocol_names: t.protocols.clone(),
        }
    }
}

// ─── NpParam ────────────────────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct NpParam {
    pub par_type: Option<Box<NpType>>,
    pub name: Option<String>,
    pub next: Option<Box<NpParam>>,
}

// ─── Symbol ─────────────────────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub enum SymbolData {
    Class {
        superclass: Option<String>,
        protocols: Vec<String>,
        methods: Vec<String>,
        ivars: Vec<String>,
        properties: Vec<String>,
        type_params: Vec<String>,
        /// Generic bounds: (type-param name, bound name) pairs from the
        /// `@interface` — the symbol table is the single source of truth
        /// the checker's instantiation-point conformance check consults.
        type_bounds: Vec<(String, String)>,
    },
    Method {
        is_class_method: bool,
        return_type: Option<Box<NpType>>,
        params: Option<Box<NpParam>>,
        has_body: bool,
        vtable_index: i32,
        owner_class: Option<String>,
    },
    Ivar {
        ivar_type: Option<Box<NpType>>,
        offset: i32,
        owning_class: Option<String>,
    },
    Property {
        prop_type: Option<Box<NpType>>,
        ivar_sym: Option<String>,
        getter: Option<String>,
        setter: Option<String>,
        is_readonly: bool,
        is_weak: bool,
        is_assign: bool,
        is_retain: bool,
        is_copy: bool,
        is_nonatomic: bool,
        is_dynamic: bool,
    },
    Protocol {
        parents: Vec<String>,
        required_methods: Vec<String>,
        optional_methods: Vec<String>,
    },
    Variable {
        var_type: Option<Box<NpType>>,
        is_static: bool,
        is_extern: bool,
        is_const: bool,
        is_weak: bool,
        is_block: bool,
    },
    Function {
        return_type: Option<Box<NpType>>,
        params: Option<Box<NpParam>>,
        has_variadic: bool,
    },
}

#[derive(Debug, Clone)]
pub struct Symbol {
    pub kind: SymbolKind,
    pub name: String,
    pub data: SymbolData,
}

impl Symbol {
    pub fn new(kind: SymbolKind, name: &str) -> Self {
        let data = match kind {
            SymbolKind::Class => SymbolData::Class {
                superclass: None, protocols: Vec::new(),
                methods: Vec::new(), ivars: Vec::new(),
                properties: Vec::new(), type_params: Vec::new(),
                type_bounds: Vec::new(),
            },
            SymbolKind::Method => SymbolData::Method {
                is_class_method: false, return_type: None,
                params: None, has_body: false, vtable_index: -1,
                owner_class: None,
            },
            SymbolKind::Ivar => SymbolData::Ivar {
                ivar_type: None, offset: 0, owning_class: None,
            },
            SymbolKind::Property => SymbolData::Property {
                prop_type: None, ivar_sym: None,
                getter: None, setter: None,
                is_readonly: false, is_weak: false, is_assign: false,
                is_retain: false, is_copy: false, is_nonatomic: false,
                is_dynamic: false,
            },
            SymbolKind::Protocol => SymbolData::Protocol {
                parents: Vec::new(), required_methods: Vec::new(),
                optional_methods: Vec::new(),
            },
            SymbolKind::Variable => SymbolData::Variable {
                var_type: None, is_static: false, is_extern: false,
                is_const: false, is_weak: false, is_block: false,
            },
            SymbolKind::Function => SymbolData::Function {
                return_type: None, params: None, has_variadic: false,
            },
            SymbolKind::Type | SymbolKind::Selector => {
                // These kinds have no data
                SymbolData::Variable { var_type: None, is_static: false, is_extern: false, is_const: false, is_weak: false, is_block: false }
            }
        };
        Symbol { kind, name: name.to_string(), data }
    }
}

// ─── Scope ──────────────────────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct Scope {
    pub parent: Option<Box<Scope>>,
    pub symbols: Vec<Symbol>,
}

impl Scope {
    pub fn new(parent: Option<Box<Scope>>) -> Self {
        Scope { parent, symbols: Vec::new() }
    }

    pub fn add(&mut self, sym: Symbol) {
        self.symbols.push(sym);
    }

    pub fn lookup(&self, name: &str) -> Option<&Symbol> {
        for s in &self.symbols {
            if s.name == name { return Some(s); }
        }
        self.parent.as_ref().and_then(|p| p.lookup(name))
    }

    pub fn lookup_local(&self, name: &str) -> Option<&Symbol> {
        self.symbols.iter().find(|s| s.name == name)
    }
}

// ─── UsingEntry ─────────────────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct UsingEntry {
    pub fqn: String,
    pub short_name: String,
    pub ptr_level: usize,
    pub is_namespace: bool,
}

// ─── SymbolTable ────────────────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct SymbolTable {
    pub global: Scope,
    pub current: Scope,
    pub selectors: Vec<Symbol>,
    pub using_list: Vec<UsingEntry>,
    stack: Vec<Scope>,
}

impl SymbolTable {
    pub fn new() -> Self {
        SymbolTable {
            global: Scope::new(None),
            current: Scope::new(None),
            selectors: Vec::new(),
            using_list: Vec::new(),
            stack: Vec::new(),
        }
    }

    pub fn push_scope(&mut self) {
        let new_scope = Scope::new(Some(Box::new(self.current.clone())));
        self.stack.push(self.current.clone());
        self.current = new_scope;
    }

    pub fn pop_scope(&mut self) {
        if let Some(scope) = self.stack.pop() {
            self.current = scope;
        }
    }

    pub fn declare(&mut self, sym: Symbol) {
        let kind = sym.kind;
        // Global declarations go to global scope
        if matches!(kind, SymbolKind::Class | SymbolKind::Protocol | SymbolKind::Type | SymbolKind::Function) {
            self.global.add(sym.clone());
        }
        self.current.add(sym);
    }

    pub fn lookup(&self, name: &str) -> Option<&Symbol> {
        self.current.lookup(name)
            .or_else(|| self.global.lookup(name))
    }

    pub fn lookup_local(&self, name: &str) -> Option<&Symbol> {
        self.current.lookup_local(name)
    }

    pub fn find_class(&self, name: &str) -> Option<&Symbol> {
        self.global.symbols.iter().find(|s| s.name == name && s.kind == SymbolKind::Class)
    }

    pub fn find_protocol(&self, name: &str) -> Option<&Symbol> {
        self.global.symbols.iter().find(|s| s.name == name && s.kind == SymbolKind::Protocol)
    }

    pub fn find_type(&self, name: &str) -> Option<&Symbol> {
        self.global.symbols.iter().find(|s| s.name == name && s.kind == SymbolKind::Type)
    }

    pub fn register_selector(&mut self, name: &str) {
        if !self.selectors.iter().any(|s| s.name == name) {
            self.selectors.push(Symbol::new(SymbolKind::Selector, name));
        }
    }

    pub fn find_selector(&self, name: &str) -> Option<&Symbol> {
        self.selectors.iter().find(|s| s.name == name)
    }

    pub fn add_using(&mut self, fqn: &str, short_name: &str, ptr_level: usize, is_namespace: bool) {
        self.using_list.push(UsingEntry {
            fqn: fqn.to_string(),
            short_name: short_name.to_string(),
            ptr_level,
            is_namespace,
        });
    }

    pub fn find_using(&self, short_name: &str) -> Option<&UsingEntry> {
        self.using_list.iter().find(|u| u.short_name == short_name)
    }
}