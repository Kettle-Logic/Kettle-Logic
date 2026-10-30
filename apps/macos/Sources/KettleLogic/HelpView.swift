import AppKit
import SwiftUI
import WebKit

/// One document of the bundled docs.
struct HelpPage: Identifiable, Hashable {
    /// The path under the docs folder, such as "formats/beerxml-import.md".
    let id: String
    let url: URL
    let title: String
}

/// The user guide: the Markdown pages in the app's `Help` folder.
enum HelpLibrary {
    /// The Help folder in the app bundle. A plain `swift run` has no bundle, so it reads the source folder.
    static let root: URL? = {
        if let bundled = Bundle.main.resourceURL?.appending(path: "Help"),
           FileManager.default.fileExists(atPath: bundled.path) {
            return bundled
        }
        // This file is apps/macos/Sources/KettleLogic/HelpView.swift, so the app's folder is three levels up.
        let source = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .appending(path: "Help")
        return FileManager.default.fileExists(atPath: source.path) ? source : nil
    }()

    static var pages: [HelpPage] {
        guard let root, let files = FileManager.default.enumerator(at: root, includingPropertiesForKeys: nil) else {
            return []
        }
        var pages: [HelpPage] = []
        for case let url as URL in files where url.pathExtension == "md" {
            guard let id = id(for: url) else { continue }
            pages.append(HelpPage(id: id, url: url, title: title(of: url)))
        }
        return pages.sorted { $0.id < $1.id }
    }

    /// The page id of a file in the docs folder, or nil for a file outside it.
    static func id(for url: URL) -> String? {
        guard let root else { return nil }
        let base = root.standardizedFileURL.pathComponents
        let path = url.standardizedFileURL.pathComponents
        guard path.count > base.count, path.starts(with: base) else { return nil }
        return path.dropFirst(base.count).joined(separator: "/")
    }

    /// The first top-level heading of a document, or its file name if it has none.
    private static func title(of url: URL) -> String {
        let text = (try? String(contentsOf: url, encoding: .utf8)) ?? ""
        let heading = text.components(separatedBy: "\n").first { $0.hasPrefix("# ") }
        return heading.map { String($0.dropFirst(2)) } ?? url.deletingPathExtension().lastPathComponent
    }
}

/// The Help window: the documents in a sidebar, and the selected one beside it.
struct HelpView: View {
    @State private var pages = HelpLibrary.pages
    @State private var selection: String?

    var body: some View {
        NavigationSplitView {
            List(pages, selection: $selection) { page in
                Text(page.title)
            }
            .navigationSplitViewColumnWidth(min: 170, ideal: 210)
        } detail: {
            if let page = pages.first(where: { $0.id == selection }) {
                HelpWebView(page: page) { selection = $0 }
            } else {
                ContentUnavailableView(
                    "Help Is Missing",
                    systemImage: "questionmark.circle",
                    description: Text("The documentation was not found in this copy of the app.")
                )
            }
        }
        .frame(minWidth: 780, minHeight: 500)
        .onAppear { selection = selection ?? pages.first?.id }
    }
}

/// Shows one document. A link to another document opens it in the same view, and a web link opens in the browser.
struct HelpWebView: NSViewRepresentable {
    let page: HelpPage
    let open: (String) -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator(open: open)
    }

    func makeNSView(context: Context) -> WKWebView {
        let view = WKWebView()
        view.navigationDelegate = context.coordinator
        return view
    }

    func updateNSView(_ view: WKWebView, context: Context) {
        context.coordinator.open = open
        guard context.coordinator.shownID != page.id else { return }
        context.coordinator.shownID = page.id
        let markdown = (try? String(contentsOf: page.url, encoding: .utf8)) ?? ""
        // The folder of the page is the base, so relative links such as "../calc.md" resolve.
        view.loadHTMLString(
            HelpMarkdown.html(for: markdown, title: page.title),
            baseURL: page.url.deletingLastPathComponent()
        )
    }

    @MainActor
    final class Coordinator: NSObject, WKNavigationDelegate {
        var open: (String) -> Void
        var shownID: String?

        init(open: @escaping (String) -> Void) {
            self.open = open
        }

        func webView(
            _ webView: WKWebView,
            decidePolicyFor action: WKNavigationAction,
            decisionHandler: @escaping @MainActor (WKNavigationActionPolicy) -> Void
        ) {
            guard action.navigationType == .linkActivated, let url = action.request.url else {
                decisionHandler(.allow)
                return
            }
            decisionHandler(.cancel)
            if url.isFileURL, url.pathExtension == "md", let id = HelpLibrary.id(for: url) {
                open(id)
            } else if ["http", "https"].contains(url.scheme ?? "") {
                NSWorkspace.shared.open(url)
            }
        }
    }
}
