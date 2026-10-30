import Foundation

/// Turns the Markdown of the bundled docs into a web page for the Help window. It handles the
/// subset those files use: headings, paragraphs, nested lists, code blocks, tables, block quotes,
/// rules, and inline code, bold, italic, and links.
enum HelpMarkdown {
    /// A complete HTML page for one document. The colors follow light or dark mode.
    static func html(for markdown: String, title: String) -> String {
        """
        <!doctype html>
        <html>
        <head>
        <meta charset="utf-8">
        <title>\(escape(title))</title>
        <style>\(style)</style>
        </head>
        <body>
        \(blocks(of: markdown))
        </body>
        </html>
        """
    }

    private static func blocks(of markdown: String) -> String {
        let lines = markdown.components(separatedBy: "\n")
        var out: [String] = []
        var paragraph: [String] = []
        var lists: [(indent: Int, ordered: Bool)] = []
        var index = 0

        func flushParagraph() {
            if !paragraph.isEmpty {
                out.append("<p>\(inline(paragraph.joined(separator: " ")))</p>")
                paragraph = []
            }
        }

        // Closes the lists indented deeper than `indent`. Each closes its last item too.
        func closeLists(to indent: Int = -1) {
            while let list = lists.last, list.indent > indent {
                out.append("</li></\(list.ordered ? "ol" : "ul")>")
                lists.removeLast()
            }
        }

        func addItem(indent: Int, ordered: Bool, text: String) {
            closeLists(to: indent)
            if let list = lists.last, list.indent == indent, list.ordered == ordered {
                out.append("</li>")
            } else {
                // A list of the other kind at this depth ends, and a new one starts.
                closeLists(to: indent - 1)
                out.append(ordered ? "<ol>" : "<ul>")
                lists.append((indent, ordered))
            }
            out.append("<li>\(inline(text))")
        }

        while index < lines.count {
            let line = lines[index]
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            index += 1

            if trimmed.isEmpty {
                flushParagraph()
            } else if trimmed.hasPrefix("```") {
                flushParagraph()
                closeLists()
                var code: [String] = []
                while index < lines.count, !lines[index].trimmingCharacters(in: .whitespaces).hasPrefix("```") {
                    code.append(lines[index])
                    index += 1
                }
                index += 1  // The closing fence.
                out.append("<pre><code>\(escape(code.joined(separator: "\n")))</code></pre>")
            } else if let level = headingLevel(trimmed) {
                flushParagraph()
                closeLists()
                out.append("<h\(level)>\(inline(String(trimmed.dropFirst(level + 1))))</h\(level)>")
            } else if ["---", "***", "___"].contains(trimmed) {
                flushParagraph()
                closeLists()
                out.append("<hr>")
            } else if trimmed.hasPrefix(">") {
                flushParagraph()
                closeLists()
                var quote = [String(trimmed.dropFirst()).trimmingCharacters(in: .whitespaces)]
                while index < lines.count, lines[index].trimmingCharacters(in: .whitespaces).hasPrefix(">") {
                    quote.append(String(lines[index].trimmingCharacters(in: .whitespaces).dropFirst()).trimmingCharacters(in: .whitespaces))
                    index += 1
                }
                out.append("<blockquote><p>\(inline(quote.joined(separator: " ")))</p></blockquote>")
            } else if trimmed.hasPrefix("|"), index < lines.count, isTableRule(lines[index]) {
                flushParagraph()
                closeLists()
                let header = cells(of: trimmed)
                index += 1
                var rows: [[String]] = []
                while index < lines.count, lines[index].trimmingCharacters(in: .whitespaces).hasPrefix("|") {
                    rows.append(cells(of: lines[index]))
                    index += 1
                }
                let headHTML = header.map { "<th>\(inline($0))</th>" }.joined()
                let rowsHTML = rows.map { row in
                    "<tr>" + row.map { "<td>\(inline($0))</td>" }.joined() + "</tr>"
                }.joined()
                out.append("<table><thead><tr>\(headHTML)</tr></thead><tbody>\(rowsHTML)</tbody></table>")
            } else if let item = listItem(line) {
                flushParagraph()
                addItem(indent: item.indent, ordered: item.ordered, text: item.text)
            } else if !lists.isEmpty, line.hasPrefix(" "), out.last?.hasPrefix("<li>") == true {
                // An indented line continues the list item before it.
                out[out.count - 1] += " \(inline(trimmed))"
            } else {
                if paragraph.isEmpty {
                    closeLists()
                }
                paragraph.append(trimmed)
            }
        }
        flushParagraph()
        closeLists()
        return out.joined(separator: "\n")
    }

    private static func headingLevel(_ line: String) -> Int? {
        let hashes = line.prefix(while: { $0 == "#" }).count
        guard (1...6).contains(hashes), line.dropFirst(hashes).hasPrefix(" ") else { return nil }
        return hashes
    }

    private static func listItem(_ line: String) -> (indent: Int, ordered: Bool, text: String)? {
        let indent = line.prefix(while: { $0 == " " }).count
        let rest = line.dropFirst(indent)
        if rest.hasPrefix("- ") || rest.hasPrefix("* ") || rest.hasPrefix("+ ") {
            return (indent, false, String(rest.dropFirst(2)))
        }
        let digits = rest.prefix(while: { $0.isNumber }).count
        guard digits > 0, rest.dropFirst(digits).hasPrefix(". ") else { return nil }
        return (indent, true, String(rest.dropFirst(digits + 2)))
    }

    private static func isTableRule(_ line: String) -> Bool {
        let text = line.trimmingCharacters(in: .whitespaces)
        return text.hasPrefix("|") && text.contains("-") && text.allSatisfy { "|-: ".contains($0) }
    }

    private static func cells(of row: String) -> [String] {
        var text = row.trimmingCharacters(in: .whitespaces)
        if text.hasPrefix("|") { text.removeFirst() }
        if text.hasSuffix("|") { text.removeLast() }
        return text.components(separatedBy: "|").map { $0.trimmingCharacters(in: .whitespaces) }
    }

    /// Escapes the text first, so that nothing in a document can become markup. The backtick
    /// pieces are code and get no further formatting.
    private static func inline(_ text: String) -> String {
        let pieces = escape(text).components(separatedBy: "`")
        return pieces.enumerated().map { index, piece in
            index % 2 == 1 ? "<code>\(piece)</code>" : emphasis(piece)
        }.joined()
    }

    private static func emphasis(_ text: String) -> String {
        var result = replacing(text, #"\[([^\]]+)\]\(([^)\s]+)\)"#, #"<a href="$2">$1</a>"#)
        result = replacing(result, #"\*\*(.+?)\*\*"#, "<strong>$1</strong>")
        result = replacing(result, #"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])"#, "<em>$1</em>")
        result = replacing(result, #"(?<![\w_])_(?!\s)(.+?)(?<!\s)_(?![\w_])"#, "<em>$1</em>")
        return result
    }

    private static func replacing(_ text: String, _ pattern: String, _ template: String) -> String {
        let regex = try! NSRegularExpression(pattern: pattern)
        return regex.stringByReplacingMatches(
            in: text, range: NSRange(text.startIndex..., in: text), withTemplate: template)
    }

    private static func escape(_ text: String) -> String {
        text.replacingOccurrences(of: "&", with: "&amp;")
            .replacingOccurrences(of: "<", with: "&lt;")
            .replacingOccurrences(of: ">", with: "&gt;")
            .replacingOccurrences(of: "\"", with: "&quot;")
    }

    private static let style = """
        :root { color-scheme: light dark; }
        body { font: 14px/1.55 -apple-system, sans-serif; color: CanvasText; background: Canvas;
               max-width: 720px; margin: 0 auto; padding: 24px 28px 48px; }
        h1 { font-size: 24px; margin: 0 0 16px; }
        h2 { font-size: 19px; margin: 28px 0 8px; }
        h3 { font-size: 15px; margin: 20px 0 6px; }
        p, ul, ol, pre, table, blockquote { margin: 10px 0; }
        li { margin: 3px 0; }
        code { font: 12px ui-monospace, Menlo, monospace; padding: 1px 4px; border-radius: 4px;
               background: color-mix(in srgb, CanvasText 9%, transparent); }
        pre { padding: 10px 12px; border-radius: 8px; overflow-x: auto;
              background: color-mix(in srgb, CanvasText 6%, transparent); }
        pre code { padding: 0; background: none; }
        table { display: block; overflow-x: auto; border-collapse: collapse; }
        th, td { border: 1px solid color-mix(in srgb, CanvasText 20%, transparent);
                 padding: 4px 8px; text-align: left; vertical-align: top; }
        blockquote { margin-left: 0; padding-left: 12px; color: GrayText;
                     border-left: 3px solid color-mix(in srgb, CanvasText 25%, transparent); }
        hr { border: 0; border-top: 1px solid color-mix(in srgb, CanvasText 15%, transparent); }
        a { color: LinkText; }
        """
}
