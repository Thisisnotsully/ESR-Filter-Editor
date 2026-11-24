"""Finds each rule's text span in a filter file (line comments are not part of JSON)."""
import json, re

def strip_comments(text):
    return "\n".join(l for l in text.splitlines() if not l.strip().startswith('//'))

def rule_spans(text):
    """[(rule_id, start, end)] for every rule, by its "comment": "R### |" line; end = next rule's start (or the rules array end)."""
    starts = []
    for m in re.finditer(r'(?m)^[ \t]*(?://[^\n]*\n[ \t]*)*\{[ \t]*\n?[ \t]*"comment": "(R\d+) \|', text):
        # back up to the '{' that opens the rule (not the comment lines above it)
        brace = text.index('{', m.start())
        starts.append((m.group(1), brace))
    spans = []
    for i, (rid, s) in enumerate(starts):
        e = starts[i + 1][1] if i + 1 < len(starts) else text.rindex(']')
        spans.append((rid, s, e))
    return spans

def set_list(span_text, key, values):
    """Rewrites "key": [...] inside one rule's text in the list's own layout: one line stays one
    line; a wrapped list is wrapped again with the same indent and as many values per line."""
    pat = re.compile(r'("%s":\s*)\[([^\]]*)\]' % re.escape(key))
    def render(m):
        body = m.group(2)
        items = [json.dumps(v, ensure_ascii=False) for v in values]
        if '\n' not in body:
            return m.group(1) + '[' + ', '.join(items) + ']'
        lines = [l for l in body.split('\n') if l.strip()]
        indent = re.match(r'[ \t]*', lines[0]).group(0)
        per = max(1, len(re.findall(r'"[^"]*"', lines[0])))
        close = re.match(r'[ \t]*', body.split('\n')[-1]).group(0)
        rows = [indent + ', '.join(items[i:i + per]) for i in range(0, len(items), per)]
        return m.group(1) + '[\n' + ',\n'.join(rows) + '\n' + close + ']'
    out, n = pat.subn(render, span_text, count=1)
    if n != 1: raise ValueError(f'{key} not found')
    return out
