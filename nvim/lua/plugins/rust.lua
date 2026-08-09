if lazyvim_docs then
	-- LSP Server to use for Rust.
	-- Set to "bacon-ls" to use bacon-ls instead of rust-analyzer.
	-- only for diagnostics. The rest of LSP support will still be
	-- provided by rust-analyzer.
	vim.g.lazyvim_rust_diagnostics = "rust-analyzer"
end

local diagnostics = vim.g.lazyvim_rust_diagnostics or "rust-analyzer"

return {
	recommended = function()
		return LazyVim.extras.wants({
			ft = "rust",
			root = { "Cargo.toml", "rust-project.json" },
		})
	end,

	-- LSP for Cargo.toml
	{
		"Saecki/crates.nvim",
		event = { "BufRead Cargo.toml" },
		opts = {
			completion = {
				crates = {
					enabled = true,
				},
			},
			lsp = {
				enabled = true,
				actions = true,
				completion = true,
				hover = true,
			},
		},
	},

	-- Add Rust & related to treesitter
	{
		"nvim-treesitter/nvim-treesitter",
		opts = function(_, opts)
			vim.list_extend(opts.ensure_installed or {}, { "rust", "ron" })
		end,
	},

	-- Ensure Rust debugger is installed
	{
		"mason-org/mason.nvim",
		optional = true,
		opts = function(_, opts)
			opts.ensure_installed = opts.ensure_installed or {}
			vim.list_extend(opts.ensure_installed, { "codelldb" })
			if diagnostics == "bacon-ls" then
				vim.list_extend(opts.ensure_installed, { "bacon" })
			end
		end,
	},

	--[=[ {
		"mrcjkb/rustaceanvim",
		version = vim.fn.has("nvim-0.10.0") == 0 and "^4" or false,
		ft = { "rust" },
		opts = {
			server = {
				on_attach = function(_, bufnr)
					vim.keymap.set("n", "<space>n", vim.diagnostic.goto_next, opts)
					vim.keymap.set("n", "<space>p", vim.diagnostic.goto_prev, opts)
					vim.keymap.set("n", "<space>d", function()
						vim.cmd.RustLsp("openDocs")
					end, { desc = "Docs.rs open", buffer = bufnr })
					vim.keymap.set("n", "<space>v", function()
						vim.diagnostic.open_float()
					end, { desc = "View lsp messages", buffer = bufnr })
				end,
				default_settings = {
					-- rust-analyzer language server configuration
					["rust-analyzer"] = {
						cargo = {
							allFeatures = true,
							loadOutDirsFromCheck = true,
							buildScripts = {
								enable = true,
							},
						},
						-- Add clippy lints for Rust if using rust-analyzer
						checkOnSave = diagnostics == "rust-analyzer",
						-- Enable diagnostics if using rust-analyzer
						diagnostics = {
							enable = diagnostics == "rust-analyzer",
						},
						procMacro = {
							enable = true,
							ignored = {
								["async-trait"] = { "async_trait" },
								["napi-derive"] = { "napi" },
								["async-recursion"] = { "async_recursion" },
							},
						},
						files = {
							excludeDirs = {
								".git",
								".gitlab",
								"target",
								"venv",
								".venv",
							},
						},
					},
				},
			},
		},
		config = function(_, opts)
			if LazyVim.has("mason.nvim") then
				local registry = require("mason-registry")
				local codelldb_pkg = registry.get_package("codelldb")
				local codelldb_path = vim.fn.expand("$HOME/.local/share/nvim/mason/packages/codelldb")
				local extension_path = codelldb_path .. "/extension/"
				local adapter_path = extension_path .. "adapter/codelldb"
				local library_path = extension_path .. "lldb/lib/liblldb.dylib"

				local uname = io.popen("uname"):read("*l")
				if uname == "Linux" then
					library_path = extension_path .. "lldb/lib/liblldb.so"
				elseif uname == "Darwin" then
					library_path = extension_path .. "lldb/lib/liblldb.dylib"
				end

				opts.dap = {
					adapter = require("rustaceanvim.config").get_codelldb_adapter(adapter_path, library_path),
				}
			end
			vim.g.rustaceanvim = vim.tbl_deep_extend("keep", vim.g.rustaceanvim or {}, opts or {})
			if vim.fn.executable("rust-analyzer") == 0 then
				LazyVim.error(
					"**rust-analyzer** not found in PATH, please install it.\nhttps://rust-analyzer.github.io/",
					{ title = "rustaceanvim" }
				)
			end
		end,
	}, ]=]

	--[=[{
		"neovim/nvim-lspconfig",
		opts = {
			servers = {
				bacon_ls = {
					enabled = diagnostics == "bacon-ls",
				},
				rust_analyzer = { enabled = false },
			},
		},
	}, ]=]

	{
		"nvim-neotest/neotest",
		optional = true,
		opts = {
			adapters = {
				["rustaceanvim.neotest"] = {},
			},
		},
	},
}
