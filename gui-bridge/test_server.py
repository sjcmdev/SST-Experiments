"""Pure helper checks: no app launching, capture, OCR engine or input injection."""
import pytest
from server import diff_lines, fix_ocr, fuzzy_match


@pytest.mark.parametrize(("raw", "expected"), [
    ("1 OOE+O", "1.00E+0"),
    ("1.OOE+O -2.lE-O3", "1.00E+0 -2.1E-03"),
    ("O l Origin Solar Temperature hello O2 layer1", "O l Origin Solar Temperature hello 02 layer1"),
    ("10 20 300 1.25E-3", "10 20 300 1.25E-3"),
    ("Voltage 1l.O V", "Voltage 11.0 V"),
])
def test_fix_ocr(raw, expected):
    assert fix_ocr(raw) == expected


def test_diff():
    assert diff_lines([], ["1,2 yes"]) == ["1,2 yes"]
    assert diff_lines(["1,2 yes"], ["1,2 yes"]) == []
    assert diff_lines(["1,2 yes", "3,4 old"], ["1,2 yes", "3,4 new"]) == ["- 3,4 old", "3,4 new"]
    assert diff_lines(["1,2 yes"], []) == ["- 1,2 yes"]


def test_fuzzy():
    assert fuzzy_match("Temperature", ["Temperature range", "Temperalure", "pressure"]) == 1
    assert fuzzy_match("YES!", ["yes", "no", "Yes"], nth=1) == 2
    assert fuzzy_match("Series resistance", ["Series resislance", "resistance"]) == 0
    for query, candidates, nth in [("missing", ["Tabs"], 0), ("", ["Tabs"], 0), ("Tabs", [], 0),
                                   ("yes", ["yes"], 1), ("yes", ["yes"], -1)]:
        with pytest.raises(ValueError):
            fuzzy_match(query, candidates, nth)
