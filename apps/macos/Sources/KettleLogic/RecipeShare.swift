import AppKit
import SwiftUI
import UniformTypeIdentifiers

/// The formats a recipe can be exported in.
enum RecipeFormat: CaseIterable, Identifiable {
    case beerXML, text, pdf, beerJSON

    var id: Self { self }

    var title: String {
        switch self {
        case .beerXML: "BeerXML"
        case .text: "Text"
        case .pdf: "PDF"
        case .beerJSON: "BeerJSON"
        }
    }

    var fileExtension: String {
        switch self {
        case .beerXML: "xml"
        case .text: "txt"
        case .pdf: "pdf"
        case .beerJSON: "json"
        }
    }

    var contentType: UTType {
        UTType(filenameExtension: fileExtension) ?? .data
    }

    /// The fixed name the format is reported under, when the user has agreed to share which features are used.
    var analyticsName: String {
        switch self {
        case .beerXML: "beerxml"
        case .text: "text"
        case .pdf: "pdf"
        case .beerJSON: "beerjson"
        }
    }

    /// The engine's name for the format, where the engine writes it as text. The engine lays out the PDF itself.
    var engineFormat: String? {
        switch self {
        case .beerXML: "beerxml"
        case .text: "text"
        case .pdf: nil
        case .beerJSON: "beerjson"
        }
    }
}

/// The Share menu in the recipe toolbar. Each format can be saved to a file, or sent through the system share sheet.
struct RecipeShareMenu: View {
    let document: RecipeDocument
    let recipe: RecipeSnapshot
    let store: DocumentStore

    var body: some View {
        Menu {
            Section("Export As") {
                ForEach(RecipeFormat.allCases) { format in
                    Button("\(format.title)…") { export(format) }
                }
            }
            Section("Share As") {
                ForEach(RecipeFormat.allCases) { format in
                    Button("\(format.title)…") { share(format) }
                }
            }
        } label: {
            Label("Share", systemImage: "square.and.arrow.up")
        }
        .help("Export this recipe as a file, or share it.")
    }

    private func export(_ format: RecipeFormat) {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = "\(fileStem).\(format.fileExtension)"
        panel.allowedContentTypes = [format.contentType]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            try write(format, to: url)
            Analytics.record(.exportRecipe, ["format": .word(format.analyticsName)])
        } catch {
            store.errorMessage = error.localizedDescription
        }
    }

    private func share(_ format: RecipeFormat) {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("\(fileStem).\(format.fileExtension)")
        do {
            try write(format, to: url)
        } catch {
            store.errorMessage = error.localizedDescription
            return
        }
        guard let view = NSApp.keyWindow?.contentView else { return }
        NSSharingServicePicker(items: [url]).show(relativeTo: .zero, of: view, preferredEdge: .minY)
    }

    /// Writes the recipe in `format` to `url`.
    private func write(_ format: RecipeFormat, to url: URL) throws {
        if let engineFormat = format.engineFormat {
            try document.export(recipe.id, format: engineFormat)
                .write(to: url, atomically: true, encoding: .utf8)
        } else {
            try document.writeRecipePDF(recipe.id, to: url)
        }
    }

    /// The recipe's name, made safe for a file name.
    private var fileStem: String {
        recipe.name.replacingOccurrences(of: "/", with: "-").replacingOccurrences(of: ":", with: "-")
    }
}
