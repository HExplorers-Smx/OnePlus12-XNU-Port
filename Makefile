.PHONY: test validate package
test:
	python3 -m unittest discover -s tests -v
validate:
	python3 tools/validate_board.py boards/waffle.toml
package: test validate
	python3 tools/make_board_bundle.py boards/waffle.toml output
