return {
	{
		"nvim-treesitter/nvim-treesitter",
		opts = function(_, opts)
			vim.list_extend(opts.ensure_installed or {}, { "cpp" })
		end,
	},
	{
		"p00f/clangd_extensions.nvim",
		lazy = true,
		opts = {
			inlay_hints = { inline = false },
			ast = {
				role_icons = {
					type = "",
					declaration = "",
					expression = "",
					specifier = "",
					statement = "",
					["template argument"] = "",
				},
				kind_icons = {
					Compound = "",
					Recovery = "",
					TranslationUnit = "",
					PackExpansion = "",
					TemplateTypeParm = "",
					TemplateTemplateParm = "",
					TemplateParamObject = "",
				},
			},
		},
	},
	--[=[ {
    "neovim/nvim-lspconfig",
		config = function()
			local lspconfig = require("lspconfig")
			local clangd_ext_opts = require("lazyvim.util").opts("clangd_extensions.nvim")
			local on_attach = function(client, bufnr)
				local opts = { noremap = true, silent = true, buffer = bufnr }
				vim.keymap.set("n", "<space>n", vim.diagnostic.goto_next, opts)
				vim.keymap.set("n", "<space>p", vim.diagnostic.goto_prev, opts)
				vim.keymap.set("n", "<space>v", vim.diagnostic.open_float, opts)
			end

			lspconfig.clangd.setup(vim.tbl_deep_extend("force", clangd_ext_opts or {}, {
				filetypes = { "c", "cpp", "cxx", "cc", "h", "hpp" },
				on_attach = on_attach,
				keys = {},
				root_dir = function(fname)
					return require("lspconfig.util").root_pattern(".git", "build.sh", "CMakeLists.txt")(fname)
				end,
				capabilities = {
					offsetEncoding = { "utf-16" },
				},
				cmd = {
					"clangd",
					"--background-index",
					"--clang-tidy",
					"--header-insertion=iwyu",
					"--completion-style=detailed",
					"--function-arg-placeholders",
					"--fallback-style=llvm",
				},
				init_options = {
					usePlaceholders = true,
					completeUnimported = true,
					clangdFileStatus = true,
				},
			}))
		end,
	}, ]=]
}
