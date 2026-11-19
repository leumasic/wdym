import random
import sys

FRAGMENTS = [
    "a", "b", "the", "cousin", "ousin", "hello", "world", "▁", "def", "return", "x=1", "(", ")",
    "{", "}", "naïve", "日本語", "😀", "€", "\t", "\x01", "ẞ", "Ω", "--", "...", "'s", "0", "123",
    "über", "ä",
]


def generate_line(generator):
    parts = []
    if generator.random() < 0.4:
        parts.append(" " * generator.randint(1, 20))

    for _ in range(generator.randint(0, 8)):
        parts.append(generator.choice(FRAGMENTS))
        if generator.random() < 0.8:
            parts.append(" " * generator.choice([1, 1, 1, 2, 3, 4, 8, 17]))

    if generator.random() < 0.3:
        parts.append(" " * generator.randint(1, 20))

    return "".join(parts)


if __name__ == "__main__":
    line_count = int(sys.argv[1]) if len(sys.argv) > 1 else 30000
    generator = random.Random(7)
    sys.stdout.write("".join(generate_line(generator) + "\n" for _ in range(line_count)))
