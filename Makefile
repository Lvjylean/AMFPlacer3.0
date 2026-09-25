.PHONY: help check build run-baseline status
help:
	@python3 scripts/amf3.py --help
check:
	python3 -m unittest discover -s tests -v
build:
	python3 scripts/amf3.py build
run-baseline:
	python3 scripts/amf3.py run
status:
	python3 scripts/amf3.py status
