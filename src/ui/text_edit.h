// Single-line text editing state (caret, selection, typing, deletion), pure logic with no windows.h so it is
// unit-testable. Used by the Files rename field and the Search box. No IME composition: plain WM_CHAR text only. Owner: C.
#pragma once

#include <algorithm>
#include <string>

namespace velora::ui {

class TextEdit {
public:
    const std::wstring& Text() const { return text_; }
    size_t Caret() const { return caret_; }
    bool HasSelection() const { return anchor_ != caret_; }
    size_t SelectionStart() const { return std::min(anchor_, caret_); }
    size_t SelectionEnd() const { return std::max(anchor_, caret_); }

    void SetText(std::wstring t, bool selectAll = false) {
        text_ = std::move(t);
        caret_ = text_.size();
        anchor_ = selectAll ? 0 : caret_;
    }
    // Selects [from, to) (clamped), e.g. a file name without its extension.
    void Select(size_t from, size_t to) {
        anchor_ = std::min(from, text_.size());
        caret_ = std::min(to, text_.size());
    }
    void SelectAll() { Select(0, text_.size()); }
    void Clear() { SetText({}); }

    // Printable character (Backspace is Backspace()); control characters are ignored. Replaces the selection.
    bool Insert(wchar_t c) {
        if (c < 32 || c == 127) return false;
        DeleteSelection();
        text_.insert(text_.begin() + static_cast<std::ptrdiff_t>(caret_), c);
        anchor_ = ++caret_;
        return true;
    }
    void Backspace() {
        if (DeleteSelection()) return;
        if (caret_ == 0) return;
        text_.erase(caret_ - 1, 1);
        anchor_ = --caret_;
    }
    void Delete() {
        if (DeleteSelection()) return;
        if (caret_ >= text_.size()) return;
        text_.erase(caret_, 1);
    }
    void Left(bool extend) {
        if (!extend && HasSelection()) {
            anchor_ = caret_ = SelectionStart();
            return;
        }
        if (caret_ > 0) --caret_;
        if (!extend) anchor_ = caret_;
    }
    void Right(bool extend) {
        if (!extend && HasSelection()) {
            anchor_ = caret_ = SelectionEnd();
            return;
        }
        if (caret_ < text_.size()) ++caret_;
        if (!extend) anchor_ = caret_;
    }
    void Home(bool extend) {
        caret_ = 0;
        if (!extend) anchor_ = 0;
    }
    void End(bool extend) {
        caret_ = text_.size();
        if (!extend) anchor_ = caret_;
    }
    std::wstring SelectedText() const { return text_.substr(SelectionStart(), SelectionEnd() - SelectionStart()); }
    // Pasted text: control characters are dropped, line breaks end the paste.
    void InsertText(const std::wstring& s) {
        for (wchar_t c : s) {
            if (c == L'\r' || c == L'\n') break;
            Insert(c);
        }
    }

private:
    bool DeleteSelection() {
        if (!HasSelection()) return false;
        const size_t a = SelectionStart(), b = SelectionEnd();
        text_.erase(a, b - a);
        anchor_ = caret_ = a;
        return true;
    }

    std::wstring text_;
    size_t caret_ = 0, anchor_ = 0;
};

}  // namespace velora::ui
