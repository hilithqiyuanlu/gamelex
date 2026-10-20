.PHONY: host test switch package clean
host:
	cmake -S . -B build/host
	cmake --build build/host --parallel 4
test: host
	ctest --test-dir build/host --output-on-failure
switch:
	$(MAKE) -C switch/sysmodule
	$(MAKE) -C switch/overlay
	$(MAKE) -C switch/diagnostics
package:
	python3 scripts/package.py
clean:
	$(MAKE) -C switch/sysmodule clean
	$(MAKE) -C switch/overlay clean
	$(MAKE) -C switch/diagnostics clean
