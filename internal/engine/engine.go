package engine

import (
	"strings"
	"unicode"
)

type Engine struct {
	index map[string][]int
}

func NewEngine() *Engine {
	return &Engine{
		index: make(map[string][]int),
	}
}

// Tokenize splits the query into tokens, ensuring UTF-8 compatibility
func (e *Engine) Tokenize(text string) []string {
	return strings.FieldsFunc(text, func(r rune) bool {
		return unicode.IsSpace(r) || unicode.IsPunct(r)
	})
}

func (e *Engine) Search(query string) []int {
	tokens := e.Tokenize(query)
	if len(tokens) == 0 {
		return nil
	}

	// Simple intersection search for demonstration
	var results []int
	firstToken := tokens[0]
	if ids, ok := e.index[firstToken]; ok {
		results = ids
	}

	for i := 1; i < len(tokens); i++ {
		token := tokens[i]
		if ids, ok := e.index[token]; ok {
			results = intersect(results, ids)
		} else {
			return nil
		}
	}

	return results
}

func intersect(a, b []int) []int {
	res := []int{}
	m := make(map[int]bool)
	for _, x := range a {
		m[x] = true
	}
	for _, x := range b {
		if m[x] {
			res = append(res, x)
		}
	}
	return res
}
