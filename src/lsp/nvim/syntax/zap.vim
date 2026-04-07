if exists("b:current_syntax")
  finish
endif

syntax case match

syntax keyword zapConditional if else match
syntax keyword zapRepeat while for
syntax keyword zapStatement return break continue import as unsafe new self
syntax keyword zapStorageClass fun ext pub priv prot global const var ref module impl static
syntax keyword zapStructure record struct class enum alias

syntax region zapString start=+"+ skip=+\\.+ end=+"+ contains=zapEscape
syntax match zapEscape +\\["\\abfnrtv0]+ contained
syntax match zapEscape +\\x[0-9a-fA-F]\{2}+ contained
syntax match zapEscape +\\u[0-9a-fA-F]\{4}+ contained
syntax match zapEscape +\\U[0-9a-fA-F]\{8}+ contained
syntax region zapChar start=+'+ skip=+\\.+ end=+'+ contains=zapEscape
syntax match zapNumber "\<[0-9]\+\(\.[0-9]\+\)\?\>"
syntax keyword zapBoolean true false null

syntax match zapNamespace "\<[A-Za-z_][A-Za-z0-9_]*\>\ze\s*\."
syntax match zapFunction "\<\%(unsafe\s\+fun\|fun\|ext\s\+fun\)\s\+\zs[A-Za-z_][A-Za-z0-9_]*\>"
syntax match zapFunctionCall "\<[A-Za-z_][A-Za-z0-9_]*\>\ze\s*("
syntax match zapTypeDecl "\<\%(record\|struct\|unsafe\s\+struct\|class\|enum\|alias\)\s\+\zs[A-Za-z_][A-Za-z0-9_]*\>"
syntax match zapVarDecl "\<\%(var\|const\)\s\+\zs[A-Za-z_][A-Za-z0-9_]*\>"
syntax match zapParameter "\<\%(ref\s\+\)\?\zs[A-Za-z_][A-Za-z0-9_]*\ze\s*:"
syntax match zapType ":\s*\zs\%(\.\.\.\s*\)\?\%(\*\s*\)*[A-Za-z_][A-Za-z0-9_]*\%(\s*\.\s*[A-Za-z_][A-Za-z0-9_]*\)*\>"
syntax match zapReturnType ")\s*\zs\%(\.\.\.\s*\)\?\%(\*\s*\)*[A-Za-z_][A-Za-z0-9_]*\%(\s*\.\s*[A-Za-z_][A-Za-z0-9_]*\)*\>"
syntax match zapBuiltinType "\<\%(Int\|Int8\|Int16\|Int32\|Int64\|UInt\|UInt8\|UInt16\|UInt32\|UInt64\|Float\|Float32\|Float64\|Bool\|Void\|Char\|String\)\>"
syntax match zapLogicalOperator "&&\|||"
syntax match zapComparisonOperator "==\|!=\|>=\|<=\|>\|<"
syntax match zapAssignmentOperator "="
syntax match zapArithmeticOperator "[+\-/%^~]"
syntax match zapUnaryOperator "!"
syntax match zapPointerOperator "\%(^\|[^A-Za-z0-9_]\)\zs[&*]\ze\%([^=]\|$\)"

" Define comments after other matches so they win priority over operators and keywords.
syntax match zapLineComment "//.*$" contains=@Spell

highlight default link zapConditional Conditional
highlight default link zapRepeat Repeat
highlight default link zapStatement Statement
highlight default link zapStorageClass StorageClass
highlight default link zapStructure Structure
highlight default link zapLineComment Comment
highlight default link zapString String
highlight default link zapEscape SpecialChar
highlight default link zapChar Character
highlight default link zapNumber Number
highlight default link zapBoolean Boolean
highlight default link zapNamespace Include
highlight default link zapFunction Function
highlight default link zapFunctionCall Function
highlight default link zapTypeDecl Type
highlight default link zapVarDecl Identifier
highlight default link zapParameter Identifier
highlight default link zapType Type
highlight default link zapReturnType Type
highlight default link zapBuiltinType Type
highlight default link zapLogicalOperator Operator
highlight default link zapComparisonOperator Operator
highlight default link zapAssignmentOperator Operator
highlight default link zapArithmeticOperator Operator
highlight default link zapUnaryOperator Operator
highlight default link zapPointerOperator Operator

let b:current_syntax = "zap"
