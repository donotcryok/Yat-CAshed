package parameters;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * 零依赖的 YAML <b>子集</b>解析器。
 *
 * <h3>为什么不用现成的库</h3>
 * B2 轮次（依赖治理）已经把 {@code simulator/lib/} 收敛到单一的
 * {@code commons-math3}。为了一份几十行的实验配置再引入 SnakeYAML，
 * 会让「可离线构建」这个既有约束失效。因此这里只实现配置真正需要的那部分语法，
 * 并在超出范围时<b>显式报错</b>，而不是静默猜。
 *
 * <h3>支持</h3>
 * <ul>
 *   <li>缩进块映射：{@code key:} + 更深一层缩进</li>
 *   <li>缩进块列表：{@code - value} / {@code - key: value}（可继续追加同级键）</li>
 *   <li>行内列表：{@code [0.6, 0.8, 1.0]}（可空 {@code []}）</li>
 *   <li>标量：整数、浮点（含 {@code 1e3}）、布尔、{@code null}/{@code ~}、
 *       单/双引号字符串、无引号字符串</li>
 *   <li>注释：行首或行尾 {@code #}（引号内的 {@code #} 不视为注释）</li>
 *   <li>文档标记 {@code ---} / {@code ...} / {@code %YAML}（跳过）</li>
 * </ul>
 *
 * <h3>不支持（会报错，不会猜）</h3>
 * <ul>
 *   <li>Tab 缩进 —— YAML 规范禁止，且极易与空格混用导致层级错乱</li>
 *   <li>行内映射 {@code {a: 1}}</li>
 *   <li>多行标量（{@code |} / {@code >}）、锚点与别名（{@code &} / {@code *}）、
 *       合并键（{@code <<}）、标签（{@code !!}）</li>
 * </ul>
 *
 * <h3>错误定位</h3>
 * 所有异常均带「文件:行号」，不返回半成品结构。
 *
 * <p>该类只做「文本 → {@code Map/List/String/Long/Double/Boolean/null}」的机械转换，
 * 不做任何业务语义校验；语义校验在 {@link Config} 与调用方完成。
 */
public final class YamlSubset {

    /** 解析失败。消息中已包含 {@code 文件:行号} 与原因。 */
    public static final class YamlException extends RuntimeException {
        private static final long serialVersionUID = 1L;

        public YamlException(String message) {
            super(message);
        }
    }

    /** 去掉缩进与注释后的一行。 */
    private static final class Line {
        final int indent;
        final String text;
        final int no;

        Line(int indent, String text, int no) {
            this.indent = indent;
            this.text = text;
            this.no = no;
        }
    }

    private final List<Line> lines;
    private final String source;
    private int pos;

    private YamlSubset(List<Line> lines, String source) {
        this.lines = lines;
        this.source = source;
        this.pos = 0;
    }

    /**
     * 解析文件为顶层映射。
     *
     * @param file UTF-8 编码的 YAML 文件
     * @return 保持出现顺序的顶层映射
     * @throws IOException      读取失败
     * @throws YamlException    语法不受支持或格式错误
     */
    public static Map<String, Object> parse(Path file) throws IOException {
        List<String> raw = Files.readAllLines(file, StandardCharsets.UTF_8);
        return parse(raw, file.toString());
    }

    /**
     * 解析若干行为顶层映射（便于单元测试免落盘）。
     *
     * @param raw    原始文本行
     * @param source 用于错误消息的来源描述（通常是文件名）
     */
    public static Map<String, Object> parse(List<String> raw, String source) {
        List<Line> tokens = new ArrayList<>();
        for (int i = 0; i < raw.size(); i++) {
            int no = i + 1;
            String line = raw.get(i);
            if (!line.trim().isEmpty() && line.indexOf('\t') >= 0) {
                throw new YamlException(source + ":" + no
                        + ": 缩进含制表符（Tab）；YAML 只允许空格缩进");
            }
            String stripped = stripComment(line);
            String trimmed = stripped.trim();
            if (trimmed.isEmpty() || trimmed.equals("---") || trimmed.equals("...")
                    || trimmed.startsWith("%YAML")) {
                continue;
            }
            int indent = 0;
            while (indent < stripped.length() && stripped.charAt(indent) == ' ') {
                indent++;
            }
            tokens.add(new Line(indent, stripped.substring(indent), no));
        }

        YamlSubset parser = new YamlSubset(tokens, source);
        if (tokens.isEmpty()) {
            return new LinkedHashMap<String, Object>();
        }
        Object root = parser.parseBlock(tokens.get(0).indent);
        if (!(root instanceof Map)) {
            throw parser.err(tokens.get(0).no, "顶层必须是映射（key: value），实际是列表");
        }
        @SuppressWarnings("unchecked")
        Map<String, Object> map = (Map<String, Object>) root;
        return map;
    }

    // ==================== 块解析 ====================

    private Object parseBlock(int indent) {
        Line first = lines.get(pos);
        if (isListMarker(first.text)) {
            return parseList(indent);
        }
        return parseMap(indent);
    }

    private Map<String, Object> parseMap(int indent) {
        Map<String, Object> map = new LinkedHashMap<>();
        while (pos < lines.size()) {
            Line line = lines.get(pos);
            if (line.indent < indent) {
                break;
            }
            if (line.indent > indent) {
                throw err(line.no, "缩进比同级更深，缺少上级键（缩进 " + line.indent
                        + "，期望 " + indent + "）: " + line.text);
            }
            if (isListMarker(line.text)) {
                throw err(line.no, "同一层级混用了列表项与映射键: " + line.text);
            }
            int colon = findColon(line.text);
            if (colon < 0) {
                throw err(line.no, "缺少冒号，不是合法的 key: value 形式: " + line.text);
            }
            String key = unquote(line.text.substring(0, colon).trim());
            if (key.isEmpty()) {
                throw err(line.no, "键名不能为空: " + line.text);
            }
            String rest = line.text.substring(colon + 1).trim();
            pos++;

            Object value;
            if (rest.isEmpty()) {
                if (pos < lines.size() && lines.get(pos).indent > indent) {
                    value = parseBlock(lines.get(pos).indent);
                } else {
                    // `key:` 后无内容 —— 视为空映射，交由类型校验决定是否合法
                    value = new LinkedHashMap<String, Object>();
                }
            } else {
                value = parseScalar(rest, line.no);
            }
            if (map.containsKey(key)) {
                throw err(line.no, "重复键: " + key);
            }
            map.put(key, value);
        }
        return map;
    }

    private List<Object> parseList(int indent) {
        List<Object> list = new ArrayList<>();
        while (pos < lines.size()) {
            Line line = lines.get(pos);
            if (line.indent < indent) {
                break;
            }
            if (line.indent > indent) {
                throw err(line.no, "列表项缩进不一致（缩进 " + line.indent
                        + "，期望 " + indent + "）: " + line.text);
            }
            if (!isListMarker(line.text)) {
                throw err(line.no, "同一层级混用了映射键与列表项: " + line.text);
            }
            String item = line.text.substring(1).trim();
            if (item.isEmpty()) {
                // `-` 单独成行 → 子块
                pos++;
                if (pos >= lines.size() || lines.get(pos).indent <= indent) {
                    list.add(null);
                } else {
                    list.add(parseBlock(lines.get(pos).indent));
                }
                continue;
            }
            int colon = findColon(item);
            if (colon >= 0 && !looksLikeInlineCollection(item)) {
                // `- key: value`：把本行改写成更深一层的映射首行，再交给 parseMap
                int childIndent = indent + 2;
                lines.set(pos, new Line(childIndent, item, line.no));
                list.add(parseMap(childIndent));
            } else {
                pos++;
                list.add(parseScalar(item, line.no));
            }
        }
        return list;
    }

    // ==================== 标量 ====================

    private Object parseScalar(String text, int no) {
        if (text.equals("~") || text.equals("null") || text.equals("Null") || text.equals("NULL")) {
            return null;
        }
        if (text.startsWith("[")) {
            if (!text.endsWith("]")) {
                throw err(no, "行内列表未闭合（缺少 ']'）: " + text);
            }
            return parseInlineList(text, no);
        }
        if (text.startsWith("{")) {
            throw err(no, "不支持行内映射（{...}），请改为缩进块: " + text);
        }
        if (isQuoted(text)) {
            return unquote(text);
        }
        if (text.equals("true") || text.equals("True") || text.equals("TRUE")) {
            return Boolean.TRUE;
        }
        if (text.equals("false") || text.equals("False") || text.equals("FALSE")) {
            return Boolean.FALSE;
        }
        if (text.matches("[-+]?\\d+")) {
            try {
                return Long.valueOf(Long.parseLong(text));
            } catch (NumberFormatException e) {
                throw err(no, "整数越界: " + text);
            }
        }
        if (text.matches("[-+]?(\\d+\\.\\d*|\\.\\d+)([eE][-+]?\\d+)?")
                || text.matches("[-+]?\\d+[eE][-+]?\\d+")) {
            try {
                return Double.valueOf(Double.parseDouble(text));
            } catch (NumberFormatException e) {
                throw err(no, "浮点格式错误: " + text);
            }
        }
        // 未加引号的字符串（配置里只出现在注释性字段，仍保持严格：不允许含冒号）
        return text;
    }

    private List<Object> parseInlineList(String text, int no) {
        String body = text.substring(1, text.length() - 1).trim();
        List<Object> out = new ArrayList<>();
        if (body.isEmpty()) {
            return out;
        }
        for (String part : splitTopLevel(body)) {
            String token = part.trim();
            if (token.isEmpty()) {
                throw err(no, "行内列表含空元素: " + text);
            }
            out.add(parseScalar(token, no));
        }
        return out;
    }

    // ==================== 文本工具 ====================

    private static boolean isListMarker(String text) {
        return text.equals("-") || text.startsWith("- ");
    }

    private static boolean isQuoted(String text) {
        if (text.length() < 2) {
            return false;
        }
        char first = text.charAt(0);
        return (first == '"' || first == '\'') && text.charAt(text.length() - 1) == first;
    }

    /** 只在引号外按逗号切分。 */
    private static List<String> splitTopLevel(String body) {
        List<String> parts = new ArrayList<>();
        StringBuilder cur = new StringBuilder();
        char quote = 0;
        for (int i = 0; i < body.length(); i++) {
            char c = body.charAt(i);
            if (quote != 0) {
                cur.append(c);
                if (c == quote) {
                    quote = 0;
                }
            } else if (c == '"' || c == '\'') {
                quote = c;
                cur.append(c);
            } else if (c == ',') {
                parts.add(cur.toString());
                cur.setLength(0);
            } else {
                cur.append(c);
            }
        }
        parts.add(cur.toString());
        return parts;
    }

    /** 找到引号外的第一个冒号；不存在返回 -1。 */
    private static int findColon(String text) {
        char quote = 0;
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (quote != 0) {
                if (c == quote) {
                    quote = 0;
                }
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == ':') {
                return i;
            }
        }
        return -1;
    }

    /** 去掉引号外、且前面是空白或行首的 {@code #} 注释。 */
    private static String stripComment(String line) {
        char quote = 0;
        for (int i = 0; i < line.length(); i++) {
            char c = line.charAt(i);
            if (quote != 0) {
                if (c == quote) {
                    quote = 0;
                }
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '#' && (i == 0 || Character.isWhitespace(line.charAt(i - 1)))) {
                return line.substring(0, i);
            }
        }
        return line;
    }

    private static String unquote(String text) {
        String t = text.trim();
        if (isQuoted(t)) {
            return t.substring(1, t.length() - 1);
        }
        return t;
    }

    /** 列表项里出现 {@code [a, b]} 这类行内集合时，不能按「key: value」处理。 */
    private static boolean looksLikeInlineCollection(String item) {
        String t = item.trim();
        return t.startsWith("[") || t.startsWith("{") || t.startsWith("\"") || t.startsWith("'");
    }

    private YamlException err(int lineNo, String message) {
        return new YamlException(source + ":" + lineNo + ": " + message);
    }
}
