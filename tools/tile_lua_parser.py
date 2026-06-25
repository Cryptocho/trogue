def tokenize_lua(text):
    tokens = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in ' \t\n\r':
            i += 1
            continue
        if c in '{}[],=':
            tok_map = {'{': 'LBRACE', '}': 'RBRACE', '[': 'LBRACKET', ']': 'RBRACKET', ',': 'COMMA', '=': 'ASSIGN'}
            tokens.append((tok_map[c], c))
            i += 1
            continue
        if c == '-' and i + 1 < n and text[i + 1] == '-':
            while i < n and text[i] != '\n':
                i += 1
            continue
        if c == '"' or c == "'":
            quote = c
            i += 1
            s = []
            while i < n:
                ch = text[i]
                if ch == '\\' and i + 1 < n:
                    next_ch = text[i + 1]
                    if next_ch == '\\':
                        s.append('\\')
                    elif next_ch == quote:
                        s.append(quote)
                    elif next_ch == 'n':
                        s.append('\n')
                    elif next_ch == 't':
                        s.append('\t')
                    else:
                        s.append('\\')
                        s.append(next_ch)
                    i += 2
                elif ch == quote:
                    i += 1
                    break
                else:
                    s.append(ch)
                    i += 1
            tokens.append(('STRING', ''.join(s)))
            continue
        if c.isdigit() or (c == '-' and i + 1 < n and text[i + 1].isdigit()):
            j = i + 1
            while j < n and text[j].isdigit():
                j += 1
            num = int(text[i:j])
            tokens.append(('NUMBER', num))
            i = j
            continue
        if c.isalpha() or c == '_':
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] == '_'):
                j += 1
            ident = text[i:j]
            tokens.append(('IDENT', ident))
            i = j
            continue
        i += 1
    return tokens


def parse_lua_table(text):
    tokens = tokenize_lua(text)
    pos = 0

    def peek():
        return tokens[pos] if pos < len(tokens) else None

    def consume():
        nonlocal pos
        t = tokens[pos]
        pos += 1
        return t

    def expect(tok_type):
        t = consume()
        if t[0] != tok_type:
            raise ValueError(f"Expected {tok_type}, got {t}")
        return t

    def parse_value():
        t = peek()
        if t is None:
            raise ValueError("Unexpected end of input")
        if t[0] == 'STRING':
            consume()
            return t[1]
        if t[0] == 'NUMBER':
            consume()
            return t[1]
        if t[0] == 'IDENT':
            val = t[1]
            consume()
            if val == 'true':
                return True
            elif val == 'false':
                return False
            elif val == 'nil':
                return None
            else:
                raise ValueError(f"Unknown identifier: {val}")
        if t[0] == 'LBRACE':
            return parse_table()
        raise ValueError(f"Unexpected token: {t}")

    def parse_table():
        expect('LBRACE')
        result = {}
        is_array = True
        next_idx = 0

        while True:
            t = peek()
            if t is None:
                raise ValueError("Unclosed table")
            if t[0] == 'RBRACE':
                consume()
                break

            if t[0] == 'LBRACKET':
                consume()
                num_tok = expect('NUMBER')
                expect('RBRACKET')
                expect('ASSIGN')
                val = parse_value()
                result[num_tok[1]] = val
                is_array = is_array and isinstance(num_tok[1], int)
                next_idx = max(next_idx, num_tok[1] + 1)
            elif t[0] == 'IDENT':
                key = consume()[1]
                t2 = peek()
                if t2 and t2[0] == 'ASSIGN':
                    consume()
                    val = parse_value()
                    result[key] = val
                    is_array = False
                else:
                    val = key
                    if val == 'true':
                        val = True
                    elif val == 'false':
                        val = False
                    elif val == 'nil':
                        val = None
                    result[next_idx] = val
                    next_idx += 1
            elif t[0] in ('STRING', 'NUMBER', 'LBRACE'):
                val = parse_value()
                result[next_idx] = val
                next_idx += 1
            else:
                raise ValueError(f"Unexpected token in table: {t}")

            t = peek()
            if t and t[0] == 'COMMA':
                consume()

        if is_array and result:
            max_idx = max(k for k in result if isinstance(k, int))
            arr = [None] * (max_idx + 1)
            for k, v in result.items():
                if isinstance(k, int):
                    arr[k] = v
            return arr
        return result

    if peek() and peek()[0] == 'IDENT' and peek()[1] == 'return':
        consume()

    return parse_value()
