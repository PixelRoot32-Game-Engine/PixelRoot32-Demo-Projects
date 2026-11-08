#include "game/rules/RecordsText.h"

namespace top_down_city::records_text {

namespace {

/// Appends to a bounded buffer; any overflow poisons the whole write.
class Writer {
public:
    Writer(char* out, std::size_t capacity)
        : out_(out), capacity_(capacity), length_(0), ok_(out != nullptr && capacity > 0) {}

    void text(const char* s) {
        while (*s != '\0') put(*s++);
    }
    void number(unsigned value) {
        char digits[10];
        int count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0 && count < 10);
        while (count > 0) put(digits[--count]);
    }
    /// Terminates, or wipes to "" when it did not fit. @return length or 0.
    std::size_t finish() {
        if (out_ == nullptr || capacity_ == 0) return 0;
        if (!ok_) {
            out_[0] = '\0';
            return 0;
        }
        out_[length_] = '\0';
        return length_;
    }

private:
    void put(char c) {
        if (!ok_) return;
        if (length_ + 1 >= capacity_) {  // keep one byte for the terminator
            ok_ = false;
            return;
        }
        out_[length_++] = c;
    }

    char* out_;
    std::size_t capacity_;
    std::size_t length_;
    bool ok_;
};

unsigned chapterCount(std::uint8_t chapters) {
    unsigned count = 0;
    const unsigned total = static_cast<unsigned>(contract::Chapter::Count);
    for (unsigned i = 0; i < total; ++i) {
        if ((chapters & (1u << i)) != 0) ++count;
    }
    return count;
}

void chapterFraction(Writer& w, std::uint8_t chapters) {
    w.number(chapterCount(chapters));
    w.text("/");
    w.number(static_cast<unsigned>(contract::Chapter::Count));
}

}  // namespace

std::size_t bookLine(const records::Record& book, char* out, std::size_t capacity) {
    Writer w(out, capacity);
    if (book.bestStreak == 0 && book.chapters == 0 && book.bestCash == 0) {
        w.text("NO RECORDS YET");
        return w.finish();
    }
    w.text("BEST STREAK ");
    w.number(book.bestStreak);
    w.text("  $");
    w.number(book.bestCash);
    if (book.chapters != 0) {
        w.text("  CH ");
        chapterFraction(w, book.chapters);
    }
    return w.finish();
}

std::size_t improvedLine(const records::Record& book,
                         const records::Improved& improved, char* out,
                         std::size_t capacity) {
    Writer w(out, capacity);
    if (improved.chapter) {
        w.text("CHAPTER ");
        chapterFraction(w, book.chapters);
        w.text(" CLEARED");
    } else if (improved.streak) {
        w.text("NEW BEST STREAK ");
        w.number(book.bestStreak);
    } else if (improved.cash) {
        w.text("NEW BEST $");
        w.number(book.bestCash);
    } else {
        // Nothing to say; still leave a valid empty string.
        return w.finish();
    }
    return w.finish();
}

}  // namespace top_down_city::records_text
