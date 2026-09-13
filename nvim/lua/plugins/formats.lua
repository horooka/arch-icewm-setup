return {
	-- Use othree/xml.vim (proven, actively maintained)
	{
		"othree/xml.vim",
		ft = { "xml", "xaml" },
		init = function()
			vim.api.nvim_create_autocmd({ "BufRead", "BufNewFile" }, {
				pattern = "*.xaml",
				callback = function()
					vim.bo.filetype = "xml"
				end,
			})
		end,
	},

	-- Treesitter XML (if not already present)
	{
		"nvim-treesitter/nvim-treesitter",
		opts = function(_, opts)
			vim.list_extend(opts.ensure_installed or {}, { "xml" })
		end,
	},
}
