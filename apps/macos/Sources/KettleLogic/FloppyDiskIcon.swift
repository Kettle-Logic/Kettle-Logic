import SwiftUI

/// A floppy disk, drawn in the foreground color. SF Symbols has no floppy disk, so the
/// shutter and label are cut out of the body, and the icon follows the toolbar's tint.
struct FloppyDiskIcon: View {
    var body: some View {
        Canvas { context, size in
            let w = size.width
            let h = size.height
            let body = Path(roundedRect: CGRect(origin: .zero, size: size), cornerRadius: w * 0.12)
            context.fill(body, with: .color(.primary))

            var cutOut = context
            cutOut.blendMode = .destinationOut
            let shutter = CGRect(x: w * 0.26, y: h * 0.06, width: w * 0.48, height: h * 0.30)
            cutOut.fill(Path(roundedRect: shutter, cornerRadius: w * 0.03), with: .color(.black))
            let label = CGRect(x: w * 0.18, y: h * 0.50, width: w * 0.64, height: h * 0.44)
            cutOut.fill(Path(roundedRect: label, cornerRadius: w * 0.05), with: .color(.black))
        }
        .frame(width: 18, height: 18)
        .accessibilityHidden(true)
    }
}
